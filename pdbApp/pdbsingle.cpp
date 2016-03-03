#include <dbAccess.h>
#include <epicsAtomic.h>
#include <errlog.h>

#include <pv/epicsException.h>

#include "errlogstream.h"
#include "pdbsingle.h"
#include "pdb.h"

namespace pvd = epics::pvData;
namespace pva = epics::pvAccess;

size_t PDBSinglePV::ninstances;

PDBSinglePV::PDBSinglePV(DBCH& chan,
            const PDBProvider::shared_pointer& prov)
    :provider(prov)
{
    this->chan.swap(chan);
    fielddesc = PVIF::dtype(this->chan);
    epics::atomic::increment(ninstances);
}

PDBSinglePV::~PDBSinglePV()
{
    epics::atomic::decrement(ninstances);
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
}

void PDBSingleChannel::printInfo(std::ostream& out)
{
    out<<"PDB single : "<<pvname<<"\n";
}

pva::ChannelGet::shared_pointer
PDBSingleChannel::createChannelGet(
        pva::ChannelGetRequester::shared_pointer const & requester,
        pvd::PVStructure::shared_pointer const & pvRequest)
{
    pva::ChannelGet::shared_pointer ret(new PDBSingleGet(shared_from_this(), requester, pvRequest));
    requester->channelGetConnect(pvd::Status(), ret, fielddesc);
    return ret;
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
    pva::Monitor::shared_pointer ret(new PDBSingleMonitor(shared_from_this(), requester, pvRequest));
    requester->monitorConnect(pvd::Status(), ret, pv->fielddesc);
    return ret;
}

PDBSingleGet::PDBSingleGet(const PDBSingleChannel::shared_pointer &channel,
                           const pva::ChannelGetRequester::shared_pointer& requester,
                           const pvd::PVStructure::shared_pointer &pvReq)
    :channel(channel)
    ,requester(requester)
    ,changed(new pvd::BitSet(channel->fielddesc->getNumberFields()))
    ,pvf(pvd::getPVDataCreate()->createPVStructure(channel->fielddesc))
    ,pvif(PVIF::attach(channel->pv->chan, pvf))
{}

namespace {
void commonGet(PVIF *pvif, pvd::BitSet* changed)
{
    changed->clear();
    {
        DBScanLocker L(pvif->chan);
        pvif->put(*changed, DBE_VALUE|DBE_ALARM|DBE_PROPERTY, NULL);
    }
    //TODO: report unused fields as changed?
    changed->clear();
    changed->set(0);
}
}

void PDBSingleGet::get()
{
    commonGet(pvif.get(), changed.get());
    requester->getDone(pvd::Status(), shared_from_this(), pvf, changed);
}




PDBSinglePut::PDBSinglePut(const PDBSingleChannel::shared_pointer &channel,
                           const pva::ChannelPutRequester::shared_pointer &requester,
                           const pvd::PVStructure::shared_pointer &pvReq)
    :channel(channel)
    ,requester(requester)
    ,changed(new pvd::BitSet(channel->fielddesc->getNumberFields()))
    ,pvf(pvd::getPVDataCreate()->createPVStructure(channel->fielddesc))
    ,pvif(PVIF::attach(channel->pv->chan, pvf))
{}

void PDBSinglePut::put(pvd::PVStructure::shared_pointer const & value,
                       pvd::BitSet::shared_pointer const & changed)
{
    // assume value may be a different struct each time
    std::auto_ptr<PVIF> putpvif(PVIF::attach(channel->pv->chan, value));
    {
        DBScanLocker L(channel->pv->chan);
        putpvif->get(*changed);
    }
    requester->putDone(pvd::Status(), shared_from_this());
}

void PDBSinglePut::get()
{
    commonGet(pvif.get(), changed.get());
    requester->getDone(pvd::Status(), shared_from_this(), pvf, changed);
}

typedef epicsGuard<epicsMutex> Guard;

static
void pdb_single_event(void *user_arg, struct dbChannel *chan,
                      int eventsRemaining, struct db_field_log *pfl)
{
    PDBSingleMonitor::Event *evt=(PDBSingleMonitor::Event*)user_arg;
    try{
        PDBSingleMonitor::shared_pointer self(evt->self->shared_from_this());
        PDBSingleMonitor::requester_t::shared_pointer req;
        {
            Guard G(self->lock); // TODO: lock order?

            if(!self->running) return; // ignore

            self->scratch.clear();
            {
                DBScanLocker L(dbChannelRecord(self->channel->pv->chan));
                self->pvif->put(self->scratch, evt->dbe_mask, pfl);
            }

            if(self->empty.empty()){
                self->changed |= self->scratch;
                self->overflow |= self->scratch;
                self->inoverflow = true;

            } else {
                assert(!self->inoverflow);

                pvd::MonitorElementPtr elem(self->empty.front());
                elem->pvStructurePtr->copyUnchecked(*self->complete);
                *elem->changedBitSet = self->scratch;
                elem->overrunBitSet->clear(); //TODO

                if(self->inuse.empty())
                    req = self->requester;

                self->inuse.push_back(elem);
                self->empty.pop_front();
            }
        }

        if(req) {
            //TODO: in worker/pool?
            req->monitorEvent(self);
        }

    }catch(std::tr1::bad_weak_ptr&){
        /* We are racing destruction of the PDBSingleMonitor, but things are ok.
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

PDBSingleMonitor::Event::Event(PDBSingleMonitor *m, unsigned mask)
    :self(m)
    ,subscript(NULL)
    ,dbe_mask(mask)
{
    subscript = db_add_event(self->channel->pv->provider->event_context,
                             self->channel->pv->chan,
                             &pdb_single_event,
                             (void*)this,
                             mask);
    if(!subscript)
        throw std::runtime_error("Failed to subscribe to dbEvent");
}

PDBSingleMonitor::Event::~Event() {
    db_cancel_event(subscript);
}

PDBSingleMonitor::PDBSingleMonitor(const PDBSingleChannel::shared_pointer& channel,
                 const requester_t::shared_pointer& requester,
                 const pvd::PVStructure::shared_pointer& pvReq)
    :channel(channel)
    ,requester(requester)
    ,evt_VALUE(this, DBE_VALUE|DBE_ALARM)
    ,evt_PROPERTY(this, DBE_PROPERTY)
    ,inoverflow(false)
    ,running(false)
    ,nbuffers(2)
{
    pvd::PVDataCreatePtr create(pvd::getPVDataCreate());
    pvd::StructureConstPtr fielddesc(channel->pv->fielddesc);

    complete = create->createPVStructure(fielddesc);
    pvif.reset(PVIF::attach(channel->pv->chan, complete));

    empty.resize(nbuffers);
    for(size_t i=0; i<empty.size(); i++) {
        empty[i].reset(new pva::MonitorElement(create->createPVStructure(fielddesc)));
    }
}

void PDBSingleMonitor::destroy()
{
    requester_t::shared_pointer req;
    {
        Guard G(lock);
        req.swap(requester);
    }
}

pvd::Status PDBSingleMonitor::start()
{
    Guard G(lock);

    if(!running) {
        running = true;
        db_event_enable(evt_VALUE.subscript);
        db_event_enable(evt_PROPERTY.subscript);
        db_post_single_event(evt_VALUE.subscript);
        db_post_single_event(evt_PROPERTY.subscript);
        inoverflow = empty.empty();
    }
    return pvd::Status();
}

pvd::Status PDBSingleMonitor::stop()
{
    Guard G(lock);

    if(running) {
        running = false;
        db_event_disable(evt_VALUE.subscript);
        db_event_disable(evt_PROPERTY.subscript);
    }
    return pvd::Status();
}

pva::MonitorElementPtr PDBSingleMonitor::poll()
{
    Guard G(lock);
    pva::MonitorElementPtr ret;
    if(!inuse.empty()) {
        ret = inuse.front();
        inuse.pop_front();
    }
    return ret;
}

void PDBSingleMonitor::release(pva::MonitorElementPtr const & elem)
{
    PDBSingleMonitor::shared_pointer self(shared_from_this());
    PDBSingleMonitor::requester_t::shared_pointer req;
    {
        Guard G(lock);

        if(inoverflow) {
            elem->pvStructurePtr->copyUnchecked(*complete);
            *elem->changedBitSet = changed;
            *elem->overrunBitSet = overflow;

            if(self->inuse.empty())
                req = self->requester;

            inuse.push_back(elem);

        } else {
            empty.push_back(elem);
        }
    }
    if(req) {
        //TODO: in worker/pool?
        req->monitorEvent(self);
    }
}

void PDBSingleMonitor::getStats(Stats& s) const
{
    s.nempty = empty.size();
    s.nfilled = inuse.size();
    s.noutstanding = nbuffers - s.nempty - s.nfilled;
}
