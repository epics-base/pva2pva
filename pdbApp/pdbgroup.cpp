
#include <epicsAtomic.h>
#include <dbAccess.h>

#include "errlogstream.h"
#include "helper.h"
#include "pdbgroup.h"
#include "pdb.h"

namespace pvd = epics::pvData;
namespace pva = epics::pvAccess;

size_t PDBGroupPV::ninstances;

typedef epicsGuard<epicsMutex> Guard;

void pdb_group_event(void *user_arg, struct dbChannel *chan,
                     int eventsRemaining, struct db_field_log *pfl)
{
    DBEvent *evt=(DBEvent*)user_arg;
    unsigned idx = evt->index;
    try{
        PDBGroupPV::shared_pointer self(std::tr1::static_pointer_cast<PDBGroupPV>(((PDBGroupPV*)evt->self)->shared_from_this()));
        PDBGroupPV::Info& info = self->members[idx];

        {
            Guard G(self->lock); // TODO: lock order?

            if(evt->dbe_mask&DBE_PROPERTY)
            {
                DBScanLocker L(dbChannelRecord(info.chan));
                self->members[idx].pvif->put(self->scratch, evt->dbe_mask, pfl);

                if(!info.had_initial_PROPERTY) {
                    info.had_initial_PROPERTY = true;
                    self->initial_waits--;
                }
            } else {

                DBManyLocker L(info.locker); // lock only those records in the triggers list
                FOREACH(it, end, info.triggers)
                {
                    size_t i = *it;
                    LocalFL FL(i==idx ? pfl : NULL, self->members[i].chan); // for fields other than idx, creata a read fl if needed
                    self->members[i].pvif->put(self->scratch, evt->dbe_mask, FL.pfl);
                }

                if(!info.had_initial_VALUE) {
                    info.had_initial_VALUE = true;
                    self->initial_waits--;
                }
            }

            if(self->initial_waits>0) return; // don't post() until all subscriptions get initial updates

            FOREACH(it, end, self->interested) {
                PDBGroupMonitor& mon = *it->get();
                mon.post(self->scratch);
            }
            self->scratch.clear();
        }

    }catch(std::tr1::bad_weak_ptr&){
        /* We are racing destruction of the PDBGroupPV, but things are ok.
         * The destructor is running, but has not completed db_cancel_event()
         * so storage is still valid.
         * Just do nothing
         */
    }catch(std::exception& e){
        errlog_ostream strm;
        strm<<"Unhandled exception in pdb_group_event(): "<<e.what()<<"\n"
            <<SHOW_EXCEPTION(e)<<"\n";
    }
}

PDBGroupPV::PDBGroupPV()
{
    epics::atomic::increment(ninstances);
}

PDBGroupPV::~PDBGroupPV()
{
    epics::atomic::decrement(ninstances);
}

pva::Channel::shared_pointer
PDBGroupPV::connect(const std::tr1::shared_ptr<PDBProvider>& prov,
                    const pva::ChannelRequester::shared_pointer& req)
{
    pva::Channel::shared_pointer ret(new PDBGroupChannel(shared_from_this(), prov, req));
    return ret;
}

PDBGroupChannel::PDBGroupChannel(const PDBGroupPV::shared_pointer& pv,
                                 const std::tr1::shared_ptr<pva::ChannelProvider>& prov,
                                 const pva::ChannelRequester::shared_pointer& req)
    :BaseChannel(pv->name, prov, req, pv->fielddesc)
    ,pv(pv)
{}

void PDBGroupChannel::printInfo(std::ostream& out)
{
    out<<"PDB group : "<<pvname<<"\n";
}

pva::ChannelGet::shared_pointer
PDBGroupChannel::createChannelGet(
        pva::ChannelGetRequester::shared_pointer const & requester,
        pvd::PVStructure::shared_pointer const & pvRequest)
{
    pva::ChannelGet::shared_pointer ret(new PDBGroupGet(shared_from_this(), requester, pvRequest));
    requester->channelGetConnect(pvd::Status(), ret, fielddesc);
    return ret;
}

pva::ChannelPut::shared_pointer
PDBGroupChannel::createChannelPut(
        pva::ChannelPutRequester::shared_pointer const & requester,
        pvd::PVStructure::shared_pointer const & pvRequest)
{
    pva::ChannelPut::shared_pointer ret(new PDBGroupPut(shared_from_this(), requester, pvRequest));
    requester->channelPutConnect(pvd::Status(), ret, fielddesc);
    return ret;
}

pva::Monitor::shared_pointer
PDBGroupChannel::createMonitor(
        pva::MonitorRequester::shared_pointer const & requester,
        pvd::PVStructure::shared_pointer const & pvRequest)
{
    PDBGroupMonitor::shared_pointer ret(new PDBGroupMonitor(pv->shared_from_this(), requester, pvRequest));
    ret->weakself = ret;
    assert(!!pv->complete);
    ret->connect(pv->complete);
    return ret;
}


PDBGroupGet::PDBGroupGet(const PDBGroupChannel::shared_pointer &channel,
                         const pva::ChannelGetRequester::shared_pointer &requester,
                         const pvd::PVStructure::shared_pointer &pvReq)
    :channel(channel)
    ,requester(requester)
    ,atomic(false)
    ,changed(new pvd::BitSet(channel->fielddesc->getNumberFields()))
    ,pvf(pvd::getPVDataCreate()->createPVStructure(channel->fielddesc))
{
    pvd::PVScalarPtr atomicopt(pvReq->getSubField<pvd::PVScalar>("record._options.atomic"));
    if(atomicopt) {
        try {
            atomic = atomicopt->getAs<pvd::boolean>();
        }catch(std::exception& e){
            requester->message("Unable to parse 'atomic' request option.  Default is false.", pvd::warningMessage);
        }
    }

    const size_t npvs = channel->pv->members.size();
    pvif.resize(npvs);
    for(size_t i=0; i<npvs; i++)
    {
        PDBGroupPV::Info& info = channel->pv->members[i];

        pvif[i].reset(PVIF::attach(info.chan,
                               pvf->getSubFieldT<pvd::PVStructure>(info.attachment)
                               ));
    }
}

void PDBGroupGet::get()
{
    const size_t npvs = pvif.size();

    changed->clear();
    if(atomic) {
        DBManyLocker L(channel->pv->locker);
        for(size_t i=0; i<npvs; i++)
            pvif[i]->put(*changed, DBE_VALUE|DBE_ALARM|DBE_PROPERTY, NULL);
    } else {

        for(size_t i=0; i<npvs; i++)
        {
            PDBGroupPV::Info& info = channel->pv->members[i];

            DBScanLocker L(dbChannelRecord(info.chan));
            pvif[i]->put(*changed, DBE_VALUE|DBE_ALARM|DBE_PROPERTY, NULL);
        }
    }
    //TODO: report unused fields as changed?
    changed->clear();
    changed->set(0);
    requester->getDone(pvd::Status(), shared_from_this(), pvf, changed);
}


PDBGroupPut::PDBGroupPut(const PDBGroupChannel::shared_pointer& channel,
                         const pva::ChannelPutRequester::shared_pointer& requester,
                         const epics::pvData::PVStructure::shared_pointer &pvReq)
    :channel(channel)
    ,requester(requester)
    ,atomic(false)
    ,changed(new pvd::BitSet(channel->fielddesc->getNumberFields()))
    ,pvf(pvd::getPVDataCreate()->createPVStructure(channel->fielddesc))
{
    pvd::PVScalarPtr atomicopt(pvReq->getSubField<pvd::PVScalar>("record._options.atomic"));
    if(atomicopt) {
        try {
            atomic = atomicopt->getAs<pvd::boolean>();
        }catch(std::exception& e){
            requester->message("Unable to parse 'atomic' request option.  Default is false.", pvd::warningMessage);
        }
    }

    const size_t npvs = channel->pv->members.size();
    pvif.resize(npvs);
    for(size_t i=0; i<npvs; i++)
    {
        PDBGroupPV::Info& info = channel->pv->members[i];

        pvif[i].reset(PVIF::attach(info.chan,
                               pvf->getSubFieldT<pvd::PVStructure>(info.attachment)
                               ));
    }
}

void PDBGroupPut::put(pvd::PVStructure::shared_pointer const & value,
                       pvd::BitSet::shared_pointer const & changed)
{
    // assume value may be a different struct each time... lot of wasted prep work
    const size_t npvs = channel->pv->members.size();
    std::vector<std::tr1::shared_ptr<PVIF> > putpvif(npvs);

    for(size_t i=0; i<npvs; i++)
    {
        PDBGroupPV::Info& info = channel->pv->members[i];

        putpvif[i].reset(PVIF::attach(info.chan,
                               value->getSubFieldT<pvd::PVStructure>(info.attachment)
                               ));
    }

    if(atomic) {
        DBManyLocker L(channel->pv->locker);
        for(size_t i=0; i<npvs; i++)
            putpvif[i]->get(*changed);
    } else {
        for(size_t i=0; i<npvs; i++)
        {
            PDBGroupPV::Info& info = channel->pv->members[i];

            DBScanLocker L(dbChannelRecord(info.chan));
            putpvif[i]->get(*changed);
        }
    }

    requester->putDone(pvd::Status(), shared_from_this());
}

void PDBGroupPut::get()
{
    const size_t npvs = pvif.size();

    changed->clear();
    if(atomic) {
        DBManyLocker L(channel->pv->locker);
        for(size_t i=0; i<npvs; i++)
            pvif[i]->put(*changed, DBE_VALUE|DBE_ALARM|DBE_PROPERTY, NULL);
    } else {

        for(size_t i=0; i<npvs; i++)
        {
            PDBGroupPV::Info& info = channel->pv->members[i];

            DBScanLocker L(dbChannelRecord(info.chan));
            pvif[i]->put(*changed, DBE_VALUE|DBE_ALARM|DBE_PROPERTY, NULL);
        }
    }
    //TODO: report unused fields as changed?
    changed->clear();
    changed->set(0);
    requester->getDone(pvd::Status(), shared_from_this(), pvf, changed);
}

PDBGroupMonitor::PDBGroupMonitor(const PDBGroupPV::shared_pointer& pv,
                 const requester_t::shared_pointer& requester,
                 const pvd::PVStructure::shared_pointer& pvReq)
    :BaseMonitor(requester, pvReq)
    ,pv(pv)
{}

void PDBGroupMonitor::destroy()
{
    BaseMonitor::destroy();
    PDBGroupPV::shared_pointer pv;
    {
        Guard G(lock);
        this->pv.swap(pv);
    }
}

void PDBGroupMonitor::onStart()
{
    guard_t G(pv->lock);

    if(pv->interested.empty()) {
        // first subscriber
        size_t ievts = 0;
        for(size_t i=0; i<pv->members.size(); i++) {
            PDBGroupPV::Info& info = pv->members[i];

            if(!!info.evt_VALUE) {
                db_event_enable(info.evt_VALUE.subscript);
                db_post_single_event(info.evt_VALUE.subscript);
                ievts++;
                info.had_initial_VALUE = false;
            } else {
                info.had_initial_VALUE = true;
            }
            db_event_enable(info.evt_PROPERTY.subscript);
            db_post_single_event(info.evt_PROPERTY.subscript);
            ievts++;
            info.had_initial_PROPERTY = false;
        }
        pv->initial_waits = ievts;
        pv->scratch.clear();
        pv->scratch.set(0);
    } else if(pv->initial_waits==0) {
        // new subscriber and already had initial update
        post();
    } // else new subscriber, but no initial update.  so just wait

    shared_pointer self(std::tr1::static_pointer_cast<PDBGroupMonitor>(shared_from_this()));
    pv->interested.insert(self);
}

void PDBGroupMonitor::onStop()
{
    guard_t G(pv->lock);
    shared_pointer self(std::tr1::static_pointer_cast<PDBGroupMonitor>(shared_from_this()));

    if(pv->interested.erase(self)==0) {
        fprintf(stderr, "%s: oops\n", __FUNCTION__);
    }

    if(pv->interested.empty()) {
        // last subscriber
        for(size_t i=0; i<pv->members.size(); i++) {
            PDBGroupPV::Info& info = pv->members[i];

            if(!!info.evt_VALUE) {
                db_event_disable(info.evt_VALUE.subscript);
            }
            db_event_disable(info.evt_PROPERTY.subscript);
        }
    }
}

void PDBGroupMonitor::requestUpdate()
{
    Guard G(pv->lock);
    post();
}
