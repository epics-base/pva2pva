#include <dbAccess.h>
#include <epicsAtomic.h>
#include <errlog.h>

#include <pv/epicsException.h>

#include "errlogstream.h"
#include "helper.h"
#include "pdbsingle.h"
#include "pdb.h"

namespace pvd = epics::pvData;
namespace pva = epics::pvAccess;

size_t PDBSinglePV::num_instances;
size_t PDBSingleChannel::num_instances;
size_t PDBSinglePut::num_instances;
size_t PDBSingleMonitor::num_instances;

typedef epicsGuard<epicsMutex> Guard;

static
void pdb_single_event(void *user_arg, struct dbChannel *chan,
                      int eventsRemaining, struct db_field_log *pfl)
{
    DBEvent *evt=(DBEvent*)user_arg;
    try{
        PDBSinglePV::shared_pointer self(std::tr1::static_pointer_cast<PDBSinglePV>(((PDBSinglePV*)evt->self)->shared_from_this()));
        {
            Guard G(self->lock); // TODO: lock order?

            {
                DBScanLocker L(dbChannelRecord(self->chan));
                self->pvif->put(self->scratch, evt->dbe_mask, pfl);
            }

            if(evt->dbe_mask&DBE_PROPERTY)
                self->hadevent_PROPERTY = true;
            else
                self->hadevent_VALUE = true;

            if(!self->hadevent_VALUE || !self->hadevent_PROPERTY)
                return;

            FOREACH(it, end, self->interested) {
                PDBSingleMonitor& mon = *it->get();
                mon.post(self->scratch);
            }
            self->scratch.clear();
        }

    }catch(std::tr1::bad_weak_ptr&){
        /* We are racing destruction of the PDBSinglePV, but things are ok.
         * The destructor is running, but has not completed db_cancel_event()
         * so storage is still valid.
         * Just do nothing
         */
    }catch(std::exception& e){
        errlog_ostream strm;
        strm<<"Unhandled exception in pdb_single_event(): "<<e.what()<<"\n"
            <<SHOW_EXCEPTION(e)<<"\n";
    }
}

PDBSinglePV::PDBSinglePV(DBCH& chan,
            const PDBProvider::shared_pointer& prov)
    :provider(prov)
    ,evt_VALUE(this)
    ,evt_PROPERTY(this)
    ,hadevent_VALUE(false)
    ,hadevent_PROPERTY(false)
{
    this->chan.swap(chan);
    fielddesc = PVIF::dtype(this->chan);

    complete = pvd::getPVDataCreate()->createPVStructure(fielddesc);
    pvif.reset(PVIF::attach(this->chan, complete));

    epics::atomic::increment(num_instances);
}

PDBSinglePV::~PDBSinglePV()
{
    epics::atomic::decrement(num_instances);
}

void PDBSinglePV::activate()
{
    evt_VALUE.create(provider->event_context, this->chan, &pdb_single_event, DBE_VALUE|DBE_ALARM);
    evt_PROPERTY.create(provider->event_context, this->chan, &pdb_single_event, DBE_PROPERTY);
}

pva::Channel::shared_pointer
PDBSinglePV::connect(const std::tr1::shared_ptr<PDBProvider>& prov,
                     const pva::ChannelRequester::shared_pointer& req)
{
    pva::Channel::shared_pointer ret(new PDBSingleChannel(shared_from_this(), req));
    return ret;
}

PDBSingleChannel::PDBSingleChannel(const PDBSinglePV::shared_pointer& pv,
                                   const pva::ChannelRequester::shared_pointer& req)
    :BaseChannel(dbChannelName(pv->chan), pv->provider, req, pv->fielddesc)
    ,pv(pv)
{
    assert(!!this->pv);
    epics::atomic::increment(num_instances);
}

PDBSingleChannel::~PDBSingleChannel()
{
    epics::atomic::decrement(num_instances);
}

void PDBSingleChannel::printInfo(std::ostream& out)
{
    out<<"PDB single : "<<pvname<<"\n";
}

pva::ChannelPut::shared_pointer
PDBSingleChannel::createChannelPut(
        pva::ChannelPutRequester::shared_pointer const & requester,
        pvd::PVStructure::shared_pointer const & pvRequest)
{
    pva::ChannelPut::shared_pointer ret(new PDBSinglePut(shared_from_this(), requester, pvRequest));
    requester->channelPutConnect(pvd::Status(), ret, fielddesc);
    return ret;
}


pva::Monitor::shared_pointer
PDBSingleChannel::createMonitor(
        pva::MonitorRequester::shared_pointer const & requester,
        pvd::PVStructure::shared_pointer const & pvRequest)
{
    PDBSingleMonitor::shared_pointer ret(new PDBSingleMonitor(pv->shared_from_this(), requester, pvRequest));
    ret->weakself = ret;
    assert(!!pv->complete);
    ret->connect(pv->complete);
    return ret;
}


PDBSinglePut::PDBSinglePut(const PDBSingleChannel::shared_pointer &channel,
                           const pva::ChannelPutRequester::shared_pointer &requester,
                           const pvd::PVStructure::shared_pointer &pvReq)
    :channel(channel)
    ,requester(requester)
    ,changed(new pvd::BitSet(channel->fielddesc->getNumberFields()))
    ,pvf(pvd::getPVDataCreate()->createPVStructure(channel->fielddesc))
    ,pvif(PVIF::attach(channel->pv->chan, pvf))
    ,doProc(true)
    ,doProcForce(false)
    ,doWait(false)
{
    epics::atomic::increment(num_instances);
    dbChannel *chan = channel->pv->chan;
    dbCommon *precord = dbChannelRecord(chan);
    doProc = dbChannelField(chan) == &precord->proc ||
            (dbChannelFldDes(chan)->process_passive &&
             precord->scan == 0);

    pvd::boolean wait;
    getS(pvReq, "_record.options.block", wait);
    doWait = wait;
    std::string proccmd;
    if(getS(pvReq, "_record.options.process", proccmd)) {
        if(proccmd=="yes" || proccmd=="1") {
            doProc = true;
            doProcForce = true;
        } else if(proccmd=="no" || proccmd=="0") {
            doProc = false;
            doProcForce = true;
        } else if(proccmd=="passive") {
            doProcForce = false;
        }
    }

    memset((void*)&notify, 0, sizeof(notify));
    notify.usrPvt = (void*)this;
}

PDBSinglePut::~PDBSinglePut()
{
    epics::atomic::decrement(num_instances);
}

void PDBSinglePut::put(pvd::PVStructure::shared_pointer const & value,
                       pvd::BitSet::shared_pointer const & changed)
{
    dbChannel *chan = channel->pv->chan;
    dbFldDes *fld = dbChannelFldDes(chan);
    pvd::Status ret;
    if(fld->field_type>=DBF_INLINK && fld->field_type<=DBF_FWDLINK) {
        try{
            std::string lval(value->getSubFieldT<pvd::PVScalar>("value")->getAs<std::string>());
            long status = dbChannelPutField(chan, DBF_STRING, lval.c_str(), 1);
            if(status)
                ret = pvd::Status(pvd::Status::error("dbPutField() error"));
        }catch(std::exception& e) {
            std::ostringstream strm;
            strm<<"Failed to put link field "<<dbChannelName(chan)<<"."<<fld->name<<" : "<<e.what()<<"\n";
            ret = pvd::Status(pvd::Status::error(strm.str()));
        }

    } else {
        // assume value may be a different struct each time
        std::auto_ptr<PVIF> putpvif(PVIF::attach(channel->pv->chan, value));
        {
            DBScanLocker L(chan);
            putpvif->get(*changed);

            dbCommon *precord = dbChannelRecord(chan);

            bool tryproc = doProcForce ? doProc : dbChannelField(chan) == &precord->proc ||
                                                 (dbChannelFldDes(chan)->process_passive &&
                                                  precord->scan == 0);
            if (tryproc) {
                if (precord->pact) {
                    if (precord->tpro)
                        printf("%s: Active %s\n",
                               epicsThreadGetNameSelf(), precord->name);
                    precord->rpro = TRUE;
                } else {
                    /* indicate that dbPutField called dbProcess */
                    precord->putf = TRUE;
                    dbProcess(precord);
                }
            }

        }
    }
    requester_type::shared_pointer req(requester.lock());
    if(req)
        req->putDone(pvd::Status(), shared_from_this());
}

void PDBSinglePut::get()
{
    changed->clear();
    {
        DBScanLocker L(pvif->chan);
        pvif->put(*changed, DBE_VALUE|DBE_ALARM|DBE_PROPERTY, NULL);
    }
    //TODO: report unused fields as changed?
    changed->clear();
    changed->set(0);

    requester_type::shared_pointer req(requester.lock());
    if(req)
        req->getDone(pvd::Status(), shared_from_this(), pvf, changed);
}

PDBSingleMonitor::PDBSingleMonitor(const PDBSinglePV::shared_pointer& pv,
                 const requester_t::shared_pointer& requester,
                 const pvd::PVStructure::shared_pointer& pvReq)
    :BaseMonitor(requester, pvReq)
    ,pv(pv)
{
    epics::atomic::increment(num_instances);
}

PDBSingleMonitor::~PDBSingleMonitor()
{
    destroy();
    epics::atomic::decrement(num_instances);
}

void PDBSingleMonitor::destroy()
{
    BaseMonitor::destroy();
}

void PDBSingleMonitor::onStart()
{
    PDBSinglePV::shared_pointer PV(pv.lock());
    if(!PV) return;
    guard_t G(PV->lock);

    PV->scratch.clear();
    PV->scratch.set(0);
    if(PV->interested.empty()) {
        // first subscriber
        PV->hadevent_VALUE = false;
        PV->hadevent_PROPERTY = false;
        db_event_enable(PV->evt_VALUE.subscript);
        db_event_enable(PV->evt_PROPERTY.subscript);
        db_post_single_event(PV->evt_VALUE.subscript);
        db_post_single_event(PV->evt_PROPERTY.subscript);
    } else if(PV->hadevent_VALUE && PV->hadevent_PROPERTY) {
        // new subscriber and already had initial update
        post();
    } // else new subscriber, but no initial update.  so just wait

    shared_pointer self(std::tr1::static_pointer_cast<PDBSingleMonitor>(shared_from_this()));
    PV->interested.insert(self);
}

void PDBSingleMonitor::onStop()
{
    PDBSinglePV::shared_pointer PV(pv.lock());
    if(PV) return;
    guard_t G(PV->lock);
    shared_pointer self(std::tr1::static_pointer_cast<PDBSingleMonitor>(shared_from_this()));

    if(PV->interested.erase(self)==0) {
        fprintf(stderr, "%s: oops\n", __FUNCTION__);
    }

    if(PV->interested.empty()) {
        // last subscriber
        db_event_disable(PV->evt_VALUE.subscript);
        db_event_disable(PV->evt_PROPERTY.subscript);
    }
}

void PDBSingleMonitor::requestUpdate()
{
    PDBSinglePV::shared_pointer PV(pv.lock());
    if(PV) return;
    guard_t G(PV->lock);
    post();
}
