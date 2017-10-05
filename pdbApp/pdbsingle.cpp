#include <sstream>

#include <string.h>

#include <dbAccess.h>
#include <epicsAtomic.h>
#include <errlog.h>

#include <pv/epicsException.h>

#define epicsExportSharedSymbols
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
        std::cerr<<"Unhandled exception in pdb_single_event(): "<<e.what()<<"\n"
                 <<SHOW_EXCEPTION(e)<<"\n";
    }
}

PDBSinglePV::PDBSinglePV(DBCH& chan,
            const PDBProvider::shared_pointer& prov)
    :provider(prov)
    ,builder(new ScalarBuilder)
    ,evt_VALUE(this)
    ,evt_PROPERTY(this)
    ,hadevent_VALUE(false)
    ,hadevent_PROPERTY(false)
{
    this->chan.swap(chan);
    fielddesc = std::tr1::static_pointer_cast<const pvd::Structure>(builder->dtype(this->chan));

    complete = pvd::getPVDataCreate()->createPVStructure(fielddesc);
    pvif.reset(builder->attach(this->chan, complete, FieldName()));

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
    PDBSingleChannel::shared_pointer ret(new PDBSingleChannel(shared_from_this(), req));
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
    PDBSinglePut::shared_pointer ret(new PDBSinglePut(shared_from_this(), requester, pvRequest));
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


static
int single_put_callback(struct processNotify *notify,notifyPutType type)
{
    PDBSinglePut *self = (PDBSinglePut*)notify->usrPvt;

    if(notify->status!=notifyOK) return 0;

    // we've previously ensured that wait_changed&DBE_VALUE is true

    switch(type) {
    case putDisabledType:
        return 0;
    case putFieldType:
    {
        DBScanLocker L(notify->chan);
        self->wait_pvif->get(*self->wait_changed);
    }
        break;
    case putType:
        self->wait_pvif->get(*self->wait_changed);
        break;
    }
    return 1;
}

static
void single_done_callback(struct processNotify *notify)
{
    PDBSinglePut *self = (PDBSinglePut*)notify->usrPvt;
    pvd::Status sts;

    // busy state should be 1 (normal completion) or 2 (if cancel in progress)
    if(epics::atomic::compareAndSwap(self->notifyBusy, 1, 0)==0) {
        std::cerr<<"PDBSinglePut dbNotify state error?\n";
    }

    switch(notify->status) {
    case notifyOK:
        break;
    case notifyCanceled:
        return; // skip notification
    case notifyError:
        sts = pvd::Status::error("Error in dbNotify");
        break;
    case notifyPutDisabled:
        sts = pvd::Status::error("Put disabled");
        break;
    }

    PDBSinglePut::requester_type::shared_pointer req(self->requester.lock());
    if(req)
        req->putDone(sts, self->shared_from_this());
}

PDBSinglePut::PDBSinglePut(const PDBSingleChannel::shared_pointer &channel,
                           const pva::ChannelPutRequester::shared_pointer &requester,
                           const pvd::PVStructure::shared_pointer &pvReq)
    :channel(channel)
    ,requester(requester)
    ,changed(new pvd::BitSet(channel->fielddesc->getNumberFields()))
    ,pvf(pvd::getPVDataCreate()->createPVStructure(channel->fielddesc))
    ,pvif(channel->pv->builder->attach(channel->pv->chan, pvf, FieldName()))
    ,notifyBusy(0)
    ,doProc(PVIF::ProcPassive)
    ,doWait(false)
{
    epics::atomic::increment(num_instances);
    dbChannel *chan = channel->pv->chan;

    try {
        getS<pvd::boolean>(pvReq, "record._options.block", doWait);
    } catch(std::runtime_error& e) {
        requester->message(std::string("block= not understood : ")+e.what(), pva::warningMessage);
    }

    std::string proccmd;
    if(getS<std::string>(pvReq, "record._options.process", proccmd)) {
        if(proccmd=="true") {
            doProc = PVIF::ProcForce;
        } else if(proccmd=="false") {
            doProc = PVIF::ProcInhibit;
            doWait = false; // no point in waiting
        } else if(proccmd=="passive") {
            doProc = PVIF::ProcPassive;
        } else {
            requester->message("process= expects: true|false|passive", pva::warningMessage);
        }
    }

    memset((void*)&notify, 0, sizeof(notify));
    notify.usrPvt = (void*)this;
    notify.chan = chan;
    notify.putCallback = &single_put_callback;
    notify.doneCallback = &single_done_callback;
}

PDBSinglePut::~PDBSinglePut()
{
    cancel();
    epics::atomic::decrement(num_instances);
}

void PDBSinglePut::put(pvd::PVStructure::shared_pointer const & value,
                       pvd::BitSet::shared_pointer const & changed)
{
    dbChannel *chan = channel->pv->chan;
    dbFldDes *fld = dbChannelFldDes(chan);

    pvd::Status ret;
    if(dbChannelFieldType(chan)>=DBF_INLINK && dbChannelFieldType(chan)<=DBF_FWDLINK) {
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

    } else if(doWait) {
        // TODO: dbNotify doesn't allow us for force processing

        // assume value may be a different struct each time
        p2p::auto_ptr<PVIF> putpvif(channel->pv->builder->attach(channel->pv->chan, value, FieldName()));
        unsigned mask = putpvif->dbe(*changed);

        if(mask&~DBE_VALUE) {
            requester_type::shared_pointer req(requester.lock());
            if(req)
                req->message("wait=true only supports .value", pva::warningMessage);
        }

        if(epics::atomic::compareAndSwap(notifyBusy, 0, 1)!=0)
            throw std::logic_error("Previous put() not complete");

        notify.requestType = (mask&DBE_VALUE) ? putProcessRequest : processRequest;

        wait_pvif = PTRMOVE(putpvif);
        wait_changed = changed;

        dbProcessNotify(&notify);

        return; // skip notification
    } else {
        // assume value may be a different struct each time
        p2p::auto_ptr<PVIF> putpvif(channel->pv->builder->attach(channel->pv->chan, value, FieldName()));
        try{
            DBScanLocker L(chan);
            putpvif->get(*changed, doProc);

        }catch(std::runtime_error& e){
            ret = pvd::Status::error(e.what());
        }
    }
    requester_type::shared_pointer req(requester.lock());
    if(req)
        req->putDone(ret, shared_from_this());
}

void PDBSinglePut::cancel()
{
    if(epics::atomic::compareAndSwap(notifyBusy, 1, 2)==1) {
        dbNotifyCancel(&notify);
        wait_changed.reset();
        wait_pvif.reset();
        epics::atomic::set(notifyBusy, 0);
    }
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
