
#include <epicsAtomic.h>

#include "helper.h"
#include "pva2pva.h"
#include "chancache.h"

namespace pva = epics::pvAccess;
namespace pvd = epics::pvData;

size_t MonitorCacheEntry::num_instances;
size_t MonitorUser::num_instances;

namespace {
void assign(pvd::MonitorElementPtr& to, const pvd::MonitorElementPtr& from)
{
    assert(to && from);
    // TODO: lot of copying happens here.  how expensive?
    *to->changedBitSet = *from->changedBitSet;
    to->pvStructurePtr->copyUnchecked(*from->pvStructurePtr);
}

// fetch scalar value or default
template<typename T>
T getS(const pvd::PVStructurePtr& s, const char* name, T dft)
{
    try{
        return s->getSubFieldT<pvd::PVScalar>(name)->getAs<T>();
    }catch(std::runtime_error& e){
        return dft;
    }
}
}

MonitorCacheEntry::MonitorCacheEntry(ChannelCacheEntry *ent, const pvd::PVStructure::shared_pointer& pvr)
    :chan(ent)
    ,bufferSize(getS<pvd::uint32>(pvr, "record._options.queueSize", 2)) // should be same default as pvAccess, but not required
    ,done(false)
    ,nwakeups(0)
    ,nevents(0)
{
    epicsAtomicIncrSizeT(&num_instances);
}

MonitorCacheEntry::~MonitorCacheEntry()
{
    pvd::Monitor::shared_pointer M;
    M.swap(mon);
    if(M) {
        M->destroy();
    }
    epicsAtomicDecrSizeT(&num_instances);
    const_cast<ChannelCacheEntry*&>(chan) = NULL; // spoil to fault use after free
}

void
MonitorCacheEntry::monitorConnect(pvd::Status const & status,
                                  pvd::MonitorPtr const & monitor,
                                  pvd::StructureConstPtr const & structure)
{
    interested_t::vector_type tonotify;
    {
        Guard G(mutex());
        typedesc = structure;

        if(status.isSuccess()) {
            startresult = monitor->start();
        } else {
            startresult = status;
        }

        // set typedesc and startresult for futured MonitorUsers
        // and copy snapshot of already interested MonitorUsers
        tonotify = interested.lock_vector();
    }

    if(!startresult.isSuccess())
        std::cout<<"upstream monitor start() fails\n";

    shared_pointer self(weakref); // keeps us alive all MonitorUsers are destroy()ed

    for(interested_t::vector_type::const_iterator it = tonotify.begin(),
        end = tonotify.end(); it!=end; ++it)
    {
        pvd::MonitorRequester::shared_pointer req((*it)->req);
        if(req) {
            req->monitorConnect(startresult, *it, structure);
        } else {
            std::cout<<"Dead requester in monitorConnect()\n";
        }
    }
}

// notificaton from upstream client that its monitor queue has
// become not empty (transition from empty to not empty)
// will not be called again unless we completely empty the upstream queue.
// If we don't then it is our responsibly to schedule more poll().
// Note: this probably means this is a PVA client RX thread.
void
MonitorCacheEntry::monitorEvent(pvd::MonitorPtr const & monitor)
{
    /* PVA is being tricky, the Monitor* passed to monitorConnect()
     * isn't the same one we see here!
     * The original was a ChannelMonitorImpl, we now see a MonitorStrategyQueue
     * owned by the original, which delegates deserialization and accumulation
     * of deltas into complete events for us.
     * However, we don't want to keep the MonitorStrategyQueue as it's
     * destroy() method is a no-op!
     */

    epicsAtomicIncrSizeT(&nwakeups);

    shared_pointer self(weakref); // keeps us alive in case all MonitorUsers are destroy()ed

    pvd::MonitorElementPtr update;

    typedef std::vector<MonitorUser::shared_pointer> dsnotify_t;
    dsnotify_t dsnotify;

    {
        Guard G(mutex()); // MCE and MU guarded by the same mutex

        //TODO: flow control, if all MU buffers are full, break before poll()==NULL
        while((update=monitor->poll()))
        {
            epicsAtomicIncrSizeT(&nevents);

            if(lastelem)
                monitor->release(lastelem);
            lastelem = update;


            interested_t::iterator IIT(interested); // recursively locks interested.mutex() (assumes this->mutex() is interestd.mutex())
            for(interested_t::value_pointer pusr = IIT.next(); pusr; pusr = IIT.next())
            {
                MonitorUser *usr = pusr.get();

                {
                    Guard G(usr->mutex());
                    if(!usr->running || usr->empty.empty()) {
                        usr->inoverflow = true;

                        usr->overflowElement->overrunBitSet->or_and(*usr->overflowElement->changedBitSet,
                                                                    *update->changedBitSet);
                        assign(usr->overflowElement, update);

                        epicsAtomicIncrSizeT(&usr->ndropped);
                        continue;
                    }
                    // we only come out of overflow when downstream release()s an element to us
                    assert(!usr->inoverflow);

                    if(usr->filled.empty())
                        dsnotify.push_back(pusr);

                    AUTO_VAL(elem, usr->empty.front());

                    elem->overrunBitSet->clear();
                    assign(elem, update); // TODO: nice to avoid copy

                    usr->filled.push_back(elem);
                    usr->empty.pop_front();

                    epicsAtomicIncrSizeT(&usr->nevents);
                }
            }
        }
    }

    // unlock here, race w/ stop(), unlisten()?
    //TODO: notify from worker thread

    FOREACH(it,end,dsnotify) {
        MonitorUser *usr = (*it).get();
        pvd::MonitorRequester::shared_pointer req(usr->req);
        epicsAtomicIncrSizeT(&usr->nwakeups);
        req->monitorEvent(*it); // notify when first item added to empty queue, may call poll(), release(), and others
    }
}

// notificaton from upstream client that no more monitor updates will come, ever
void
MonitorCacheEntry::unlisten(pvd::MonitorPtr const & monitor)
{
    pvd::Monitor::shared_pointer M;
    interested_t::vector_type tonotify;
    {
        Guard G(mutex());
        M.swap(mon);
        tonotify = interested.lock_vector();
        // assume that upstream won't call monitorEvent() again
        monitor->release(lastelem);
        lastelem.reset();

        // cause future downstream start() to error
        startresult = pvd::Status(pvd::Status::STATUSTYPE_ERROR, "upstream unlisten()");
    }
    if(M) {
        M->destroy();
        std::cout<<__PRETTY_FUNCTION__<<" destroy client monitor\n";
    }
    FOREACH(it, end, tonotify) {
        MonitorUser *usr = it->get();
        pvd::MonitorRequester::shared_pointer req(usr->req);
        if(usr->inuse.empty()) // TODO: what about stopped?
            req->unlisten(*it);
    }
}

std::string
MonitorCacheEntry::getRequesterName()
{
    return "MonitorCacheEntry";
}

void
MonitorCacheEntry::message(std::string const & message, pvd::MessageType messageType)
{
    std::cout<<"message to Monitor cache entry about '"<<chan->channelName<<"' : "<<message<<"\n";
}

MonitorUser::MonitorUser(const MonitorCacheEntry::shared_pointer &e)
    :entry(e)
    ,running(false)
    ,inoverflow(true)
    ,nevents(0)
    ,ndropped(0)
{
    epicsAtomicIncrSizeT(&num_instances);
}

MonitorUser::~MonitorUser()
{
    epicsAtomicDecrSizeT(&num_instances);
}

// downstream server closes monitor
void
MonitorUser::destroy()
{
    {
        Guard G(mutex());
        running = false;
    }
}

pvd::Status
MonitorUser::start()
{
    pvd::MonitorRequester::shared_pointer req(this->req.lock());
    if(!req)
        return pvd::Status(pvd::Status::STATUSTYPE_FATAL, "already dead");

    bool doEvt = false;
    pvd::PVStructurePtr lval;
    pvd::StructureConstPtr typedesc;
    {
        Guard G(entry->mutex());

        if(!entry->startresult.isSuccess())
            return entry->startresult;

        if(entry->lastelem)
            lval = entry->lastelem->pvStructurePtr;
        typedesc = entry->typedesc;
    }

    {
        Guard G(mutex());

        if(empty.empty()) {
            empty.resize(entry->bufferSize);
            pvd::PVDataCreatePtr fact(pvd::getPVDataCreate());
            for(unsigned i=0; i<empty.size(); i++) {
                empty[i].reset(new pvd::MonitorElement(fact->createPVStructure(typedesc)));
            }

            // extra element to accumulate updates during overflow
            overflowElement.reset(new pvd::MonitorElement(fact->createPVStructure(typedesc)));
        }

        if(lval && !empty.empty()) {
            //already running, notify of initial element

            pvd::MonitorElementPtr elem(empty.front());
            elem->pvStructurePtr = lval;
            elem->changedBitSet->set(0); // indicate all changed?
            filled.push_back(elem);
            empty.pop_front();

            doEvt = true;
        }
        running = true;
    }
    if(doEvt)
        req->monitorEvent(shared_pointer(weakref)); // TODO: worker thread?
    return pvd::Status();
}

pvd::Status
MonitorUser::stop()
{
    Guard G(mutex());
    running = false;
    return pvd::Status::Ok;
}

pvd::MonitorElementPtr
MonitorUser::poll()
{
    Guard G(mutex());
    pvd::MonitorElementPtr ret;
    if(!filled.empty()) {
        ret = filled.front();
        inuse.insert(ret); // track which ones are out for client use
        filled.pop_front();
        //TODO: track lost buffers w/ wrapped shared_ptr?
    }
    return ret;
}

void
MonitorUser::release(pvd::MonitorElementPtr const & monitorElement)
{
    Guard G(mutex());
    //TODO: ifdef DEBUG? (only track inuse when debugging?)
    std::set<epics::pvData::MonitorElementPtr>::iterator it = inuse.find(monitorElement);
    if(it!=inuse.end()) {
        inuse.erase(it);

        if(inoverflow) { // leaving overflow condition

            // to avoid copy, enqueue the current overflowElement
            // and replace it with the element being release()d

            empty.push_back(overflowElement);
            try{
                filled.push_back(overflowElement);
            }catch(...){
                empty.pop_back();
                throw;
            }
            overflowElement = monitorElement;

            inoverflow = false;
        } else {
            // push_back empty element
            empty.push_back(monitorElement);
        }
    } else {
        // oh no, we've been given an element which we didn't give to downstream
        //TODO: check empty and filled lists to see if this is one of ours, of from somewhere else
        throw std::invalid_argument("Can't release MonitorElement not in use");
    }
    // TODO: pipeline window update?
}

std::string
MonitorUser::getRequesterName()
{
    return "MonitorCacheEntry";
}

void
MonitorUser::message(std::string const & message, pvd::MessageType messageType)
{
    std::cout<<"message to Monitor cache client : "<<message<<"\n";
}
