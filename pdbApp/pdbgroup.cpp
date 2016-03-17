
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

        {
            Guard G(self->lock); // TODO: lock order?

            self->scratch.clear();
            {
                DBScanLocker L(dbChannelRecord(self->chan[idx]));
                self->pvif[idx]->put(self->scratch, evt->dbe_mask, pfl);
            }

            self->hadevent = true;

            FOREACH(it, end, self->interested) {
                PDBGroupMonitor& mon = *it->get();
                mon.post(self->scratch);
            }
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

    const size_t npvs = channel->pv->attachments.size();
    pvif.resize(npvs);
    for(size_t i=0; i<npvs; i++)
    {
        pvif[i].reset(PVIF::attach(channel->pv->chan[i],
                               pvf->getSubFieldT<pvd::PVStructure>(channel->pv->attachments[i])
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
            DBScanLocker L(dbChannelRecord(channel->pv->chan[i]));
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

    const size_t npvs = channel->pv->attachments.size();
    pvif.resize(npvs);
    for(size_t i=0; i<npvs; i++)
    {
        pvif[i].reset(PVIF::attach(channel->pv->chan[i],
                               pvf->getSubFieldT<pvd::PVStructure>(channel->pv->attachments[i])
                               ));
    }
}

void PDBGroupPut::put(pvd::PVStructure::shared_pointer const & value,
                       pvd::BitSet::shared_pointer const & changed)
{
    // assume value may be a different struct each time... lot of wasted prep work
    const size_t npvs = channel->pv->attachments.size();
    std::vector<std::tr1::shared_ptr<PVIF> > putpvif(npvs);

    for(size_t i=0; i<npvs; i++)
    {
        putpvif[i].reset(PVIF::attach(channel->pv->chan[i],
                               value->getSubFieldT<pvd::PVStructure>(channel->pv->attachments[i])
                               ));
    }

    if(atomic) {
        DBManyLocker L(channel->pv->locker);
        for(size_t i=0; i<npvs; i++)
            putpvif[i]->get(*changed);
    } else {
        for(size_t i=0; i<npvs; i++)
        {
            DBScanLocker L(dbChannelRecord(channel->pv->chan[i]));
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
            DBScanLocker L(dbChannelRecord(channel->pv->chan[i]));
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
        pv->hadevent = false;
        for(size_t i=0; i<pv->evts_VALUE.size(); i++) {
            if(!!pv->evts_VALUE[i]) db_event_enable(pv->evts_VALUE[i].subscript);
            db_event_enable(pv->evts_PROPERTY[i].subscript);
            if(!!pv->evts_VALUE[i]) db_post_single_event(pv->evts_VALUE[i].subscript);
            db_post_single_event(pv->evts_PROPERTY[i].subscript);
        }
    } else if(pv->hadevent) {
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
        for(size_t i=0; i<pv->evts_VALUE.size(); i++) {
            if(!!pv->evts_VALUE[i]) db_event_disable(pv->evts_VALUE[i].subscript);
            db_event_disable(pv->evts_PROPERTY[i].subscript);
        }
    }
}

void PDBGroupMonitor::requestUpdate()
{
    Guard G(pv->lock);
    post();
}
