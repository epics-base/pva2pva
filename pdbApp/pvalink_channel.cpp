
#include <alarm.h>

#include <pv/reftrack.h>

#define epicsExportSharedSymbols
#include <shareLib.h>

#include "pvalink.h"

int pvaLinkNWorkers = 1;

namespace pvalink {

pvaGlobal_t *pvaGlobal;


pvaGlobal_t::pvaGlobal_t()
    :provider_local("server:QSRV")
    ,provider_remote("pva")
    ,create(pvd::getPVDataCreate())
    ,queue("PVAL")
{
    // worker should be above PVA worker priority?
    queue.start(std::max(1, pvaLinkNWorkers), epicsThreadPriorityMedium);
}

pvaGlobal_t::~pvaGlobal_t()
{
}

size_t pvaLinkChannel::num_instances;
size_t pvaLink::num_instances;


bool pvaLinkChannel::LinkSort::operator()(const pvaLink *L, const pvaLink *R) const {
    if(L->monorder==R->monorder)
        return L < R;
    return L->monorder < R->monorder;
}

// being called with pvaGlobal::lock held
pvaLinkChannel::pvaLinkChannel(const pvaGlobal_t::channels_key_t &key, const pvd::PVStructure::const_shared_pointer& pvRequest)
    :key(key)
    ,pvRequest(pvRequest)
    ,num_disconnect(0u)
    ,num_type_change(0u)
    ,connected(false)
    ,connected_latched(false)
    ,isatomic(false)
    ,queued(false)
    ,links_changed(false)
{}

pvaLinkChannel::~pvaLinkChannel() {
    {
        Guard G(pvaGlobal->lock);
        pvaGlobal->channels.erase(key);
    }

    Guard G(lock);

    assert(links.empty());
    REFTRACE_DECREMENT(num_instances);
}

void pvaLinkChannel::open()
{
    Guard G(lock);

    try {
        chan = pvaGlobal->provider_local.connect(key.first);
        TRACE(<<"Local "<<key.first);
    } catch(std::exception& e){
        errlogPrintf("failed to find in QSRV; %s\n", key.first.c_str());
    }
    if(!pvaLinkIsolate && !chan) {
        chan = pvaGlobal->provider_remote.connect(key.first);
        TRACE(<<"Remote "<<key.first);
    }

    op_mon = chan.monitor(this, pvRequest);

    REFTRACE_INCREMENT(num_instances);
}

static
pvd::StructureConstPtr putRequestType = pvd::getFieldCreate()->createFieldBuilder()
        ->addNestedStructure("field")
        ->endNested()
        ->addNestedStructure("record")
            ->addNestedStructure("_options")
                ->add("block", pvd::pvBoolean)
                ->add("process", pvd::pvString) // "true", "false", or "passive"
            ->endNested()
        ->endNested()
        ->createStructure();

// call with channel lock held
void pvaLinkChannel::put(bool force)
{
    if(!connected) return;

    pvd::PVStructurePtr pvReq(pvd::getPVDataCreate()->createPVStructure(putRequestType));
    pvReq->getSubFieldT<pvd::PVBoolean>("record._options.block")->put(false); // TODO: some way to expose completion...

    unsigned reqProcess = 0;
    bool doit = force;
    for(links_t::iterator it(links.begin()), end(links.end()); it!=end; ++it)
    {
        pvaLink *link = *it;

        if(!link->used_scratch) continue;

        pvd::shared_vector<const void> temp;
        temp.swap(link->put_scratch);
        link->used_scratch = false;
        temp.swap(link->put_queue);
        link->used_queue = true;

        doit = true;

        switch(link->pp) {
        case pvaLink::NPP:
            reqProcess |= 1;
            break;
        case pvaLink::Default:
            break;
        case pvaLink::PP:
        case pvaLink::CP:
        case pvaLink::CPP:
            reqProcess |= 2;
            break;
        }
    }

    /* By default, use remote default (passive).
     * Request processing, or not, if any link asks.
     * Prefer PP over NPP if both are specified.
     *
     * TODO: per field granularity?
     */
    const char *proc = "passive";
    if(reqProcess&2) {
        proc = "true";
    } else if(reqProcess&1) {
        proc = "false";
    }
    pvReq->getSubFieldT<pvd::PVString>("record._options.process")->put(proc);

    if(doit) {
        TRACE(<<"start");
        // start net Put, cancels in-progress put
        op_put = chan.put(this, pvReq);
    }
}

void pvaLinkChannel::putBuild(const epics::pvData::StructureConstPtr& build, pvac::ClientChannel::PutCallback::Args& args)
{
    TRACE();
    Guard G(lock);

    pvd::PVStructurePtr top(pvaGlobal->create->createPVStructure(build));

    for(links_t::iterator it(links.begin()), end(links.end()); it!=end; ++it)
    {
        pvaLink *link = *it;

        if(!link->used_queue) continue;
        link->used_queue = false; // clear early so unexpected exception won't get us in a retry loop

        pvd::PVFieldPtr value(top->getSubField("value"));
        if(!value) return; // TODO: how to signal error?

        pvd::PVStringArray::const_svector choices; // TODO populate from op_mon

        TRACE(<<"store "<<value->getFullName());
        copyDBF2PVD(link->put_queue, value, args.tosend, choices);

        link->put_queue.clear();
    }

    args.root = top;
}

void pvaLinkChannel::putDone(const pvac::PutEvent& evt)
{
    TRACE(<<evt.event<<" "<<evt.message);

    if(evt.event==pvac::PutEvent::Fail) {
        errlogPrintf("%s PVA link put ERROR: %s\n", key.first.c_str(), evt.message.c_str());
    }

    Guard G(lock);

    op_put = pvac::Operation();

    if(evt.event!=pvac::PutEvent::Success) {
        TRACE(<<"skip");

    } else {
        TRACE(<<"repeat");
        put();
    }
}

void pvaLinkChannel::monitorEvent(const pvac::MonitorEvent& evt)
{
    bool queue = false;

    {
        TRACE(<<evt.event);
        Guard G(lock);

        switch(evt.event) {
        case pvac::MonitorEvent::Disconnect:
        case pvac::MonitorEvent::Data:
            connected = evt.event == pvac::MonitorEvent::Data;
            queue = true;
            break;
        case pvac::MonitorEvent::Cancel:
            break; // no-op
        case pvac::MonitorEvent::Fail:
            connected = false;
            queue = true;
            errlogPrintf("%s: PVA link monitor ERROR: %s\n", chan.name().c_str(), evt.message.c_str());
            break;
        }

        if(queued)
            return; // already scheduled

        queued = queue;
    }

    if(queue) {
        pvaGlobal->queue.add(shared_from_this());
    }
}

// the work in calling dbProcess() which is common to
// both dbScanLock() and dbScanLockMany()
void pvaLinkChannel::run_dbProcess(size_t idx)
{
    dbCommon *precord = scan_records[idx];

    if(scan_check_passive[idx] && precord->scan!=0) {
        return;

    } else if (precord->pact) {
        if (precord->tpro)
            printf("%s: Active %s\n",
                epicsThreadGetNameSelf(), precord->name);
        precord->rpro = TRUE;

    }
    dbProcess(precord);
}

// Running from global WorkQueue thread
void pvaLinkChannel::run()
{
    bool requeue = false;
    {
        Guard G(lock);

        queued = false;

        connected_latched = connected;

        // pop next update from monitor queue.
        // still under lock to safeguard concurrent calls to lset functions
        if(connected && !op_mon.poll())
            return; // monitor queue is empty, nothing more to do here

        assert(!connected || !!op_mon.root);

        if(!connected) {
            num_disconnect++;

            // cancel pending put operations
            op_put = pvac::Operation();

            for(links_t::iterator it(links.begin()), end(links.end()); it!=end; ++it)
            {
                pvaLink *link = *it;
                link->onDisconnect();
            }

            // Don't clear previous_root on disconnect.
            // We will usually re-connect with the same type,
            // and may get back the same PVStructure.

        } else if(previous_root.get() != (const void*)op_mon.root.get()) {
            num_type_change++;

            for(links_t::iterator it(links.begin()), end(links.end()); it!=end; ++it)
            {
                pvaLink *link = *it;
                link->onTypeChange();
            }

            previous_root = std::tr1::static_pointer_cast<const void>(op_mon.root);
        }

        // at this point we know we will re-queue, but not immediately
        // so an expected error won't get us stuck in a tight loop.
        requeue = queued = true;

        if(links_changed) {
            // a link has been added or removed since the last update.
            // rebuild our cached list of records to (maybe) process.

            scan_records.clear();
            scan_check_passive.clear();

            for(links_t::iterator it(links.begin()), end(links.end()); it!=end; ++it)
            {
                pvaLink *link = *it;
                assert(link && link->alive);

                if(!link->plink) continue;

                // NPP and none/Default don't scan
                // PP, CP, and CPP do scan
                // PP and CPP only if SCAN=Passive
                if(link->pp != pvaLink::PP && link->pp != pvaLink::CPP && link->pp != pvaLink::CP)
                    continue;

                scan_records.push_back(link->plink->precord);
                scan_check_passive.push_back(link->pp != pvaLink::CP);
            }

            DBManyLock ML(scan_records);

            atomic_lock.swap(ML);

            links_changed = false;
        }

        // TODO: if connected_latched.  Option to test op_mon.changed with link::fld_value to only process on value change
    }

    if(scan_records.empty()) {
        // Nothing to do, so don't bother locking

    } else if(isatomic && scan_records.size() > 1u) {
        DBManyLocker L(atomic_lock);

        for(size_t i=0, N=scan_records.size(); i<N; i++) {
            run_dbProcess(i);
        }

    } else {
        for(size_t i=0, N=scan_records.size(); i<N; i++) {
            DBScanLocker L(scan_records[i]);
            run_dbProcess(i);
        }
    }

    if(requeue) {
        // re-queue until monitor queue is empty
        pvaGlobal->queue.add(shared_from_this());
    }
}

} // namespace pvalink
