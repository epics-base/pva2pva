#ifndef PVAHELPER_H
#define PVAHELPER_H

#include <deque>

#include <epicsGuard.h>

#include <pv/pvAccess.h>

template<typename T>
bool getS(const epics::pvData::PVStructurePtr& S, const char *name, T& val)
{
    epics::pvData::PVScalarPtr F(S->getSubField<epics::pvData::PVScalar>(name));
    if(F)
        val = F->getAs<T>();
    return !!F;
}

struct BaseChannel : public epics::pvAccess::Channel
{
    BaseChannel(const std::string& name,
                const std::tr1::shared_ptr<epics::pvAccess::ChannelProvider>& prov,
                const epics::pvAccess::ChannelRequester::shared_pointer& req,
                const epics::pvData::StructureConstPtr& dtype
                )
        :pvname(name), provider(prov), requester(req), fielddesc(dtype)
    {}
    virtual ~BaseChannel() {}

    epicsMutex lock;
    typedef epicsGuard<epicsMutex> guard_t;
    const std::string pvname;
    std::tr1::shared_ptr<epics::pvAccess::ChannelProvider> provider;
    typedef epics::pvAccess::ChannelRequester::shared_pointer requester_t;
    requester_t requester;
    const epics::pvData::StructureConstPtr fielddesc;

    // assume Requester methods not called after destory()
    virtual std::string getRequesterName() { guard_t G(lock); return requester->getRequesterName(); }

    virtual void destroy() {
        std::tr1::shared_ptr<epics::pvAccess::ChannelProvider> prov;
        requester_t req;
        {
            guard_t G(lock);
            provider.swap(prov);
            requester.swap(req);
        }
    }

    virtual std::tr1::shared_ptr<epics::pvAccess::ChannelProvider> getProvider() { guard_t G(lock); return provider; }
    virtual std::string getRemoteAddress() { guard_t G(lock); return requester->getRequesterName(); }
    virtual ConnectionState getConnectionState() { return epics::pvAccess::Channel::CONNECTED; }
    virtual std::string getChannelName() { return pvname; }
    virtual std::tr1::shared_ptr<epics::pvAccess::ChannelRequester> getChannelRequester() { guard_t G(lock); return requester; }
    virtual bool isConnected() { return getConnectionState()==epics::pvAccess::Channel::CONNECTED; }

    virtual void getField(epics::pvAccess::GetFieldRequester::shared_pointer const & requester,std::string const & subField)
    { requester->getDone(epics::pvData::Status(), fielddesc); }

    virtual epics::pvAccess::AccessRights getAccessRights(const epics::pvData::PVField::shared_pointer &pvField)
    { return epics::pvAccess::readWrite; }

    virtual epics::pvAccess::ChannelProcess::shared_pointer createChannelProcess(
            epics::pvAccess::ChannelProcessRequester::shared_pointer const & requester,
            epics::pvData::PVStructure::shared_pointer const & pvRequest)
    {
        epics::pvAccess::ChannelProcess::shared_pointer ret;
        requester->channelProcessConnect(epics::pvData::Status(epics::pvData::Status::STATUSTYPE_FATAL, "Not implemented"), ret);
        return ret;
    }

    virtual epics::pvAccess::ChannelGet::shared_pointer createChannelGet(
            epics::pvAccess::ChannelGetRequester::shared_pointer const & requester,
            epics::pvData::PVStructure::shared_pointer const & pvRequest)
    {
        epics::pvAccess::ChannelGet::shared_pointer ret;
        requester->channelGetConnect(epics::pvData::Status(epics::pvData::Status::STATUSTYPE_FATAL, "Not implemented"),
                                     ret, epics::pvData::StructureConstPtr());
        return ret;
    }

    virtual epics::pvAccess::ChannelPut::shared_pointer createChannelPut(
            epics::pvAccess::ChannelPutRequester::shared_pointer const & requester,
            epics::pvData::PVStructure::shared_pointer const & pvRequest)
    {
        epics::pvAccess::ChannelPut::shared_pointer ret;
        requester->channelPutConnect(epics::pvData::Status(epics::pvData::Status::STATUSTYPE_FATAL, "Not implemented"),
                                     ret, epics::pvData::StructureConstPtr());
        return ret;
    }

    virtual epics::pvAccess::ChannelPutGet::shared_pointer createChannelPutGet(
            epics::pvAccess::ChannelPutGetRequester::shared_pointer const & requester,
            epics::pvData::PVStructure::shared_pointer const & pvRequest)
    {
        epics::pvAccess::ChannelPutGet::shared_pointer ret;
        requester->channelPutGetConnect(epics::pvData::Status(epics::pvData::Status::STATUSTYPE_FATAL, "Not implemented"),
                                        ret, epics::pvData::StructureConstPtr(), epics::pvData::StructureConstPtr());
        return ret;
    }

    virtual epics::pvAccess::ChannelRPC::shared_pointer createChannelRPC(
            epics::pvAccess::ChannelRPCRequester::shared_pointer const & requester,
            epics::pvData::PVStructure::shared_pointer const & pvRequest)
    {
        epics::pvAccess::ChannelRPC::shared_pointer ret;
        requester->channelRPCConnect(epics::pvData::Status(epics::pvData::Status::STATUSTYPE_FATAL, "Not implemented"), ret);
        return ret;
    }

    virtual epics::pvData::Monitor::shared_pointer createMonitor(
            epics::pvData::MonitorRequester::shared_pointer const & requester,
            epics::pvData::PVStructure::shared_pointer const & pvRequest)
    {
        epics::pvData::Monitor::shared_pointer ret;
        requester->monitorConnect(epics::pvData::Status(epics::pvData::Status::STATUSTYPE_FATAL, "Not implemented"),
                                  ret, epics::pvData::StructureConstPtr());
        return ret;
    }

    virtual epics::pvAccess::ChannelArray::shared_pointer createChannelArray(
            epics::pvAccess::ChannelArrayRequester::shared_pointer const & requester,
            epics::pvData::PVStructure::shared_pointer const & pvRequest)
    {
        epics::pvAccess::ChannelArray::shared_pointer ret;
        requester->channelArrayConnect(epics::pvData::Status(epics::pvData::Status::STATUSTYPE_FATAL, "Not implemented"),
                                       ret, epics::pvData::Array::const_shared_pointer());
        return ret;
    }

    virtual void printInfo() { printInfo(std::cout); }
    virtual void printInfo(std::ostream& out) {
        out<<"Channel '"<<pvname<<"' "<<getRemoteAddress()<<"\n";
    }
};

/**
 * Helper which implements a Monitor queue.
 * connect()s to a complete copy of a PVStructure.
 * When this struct has changed, post(BitSet) should be called.
 *
 * Derived class may use onStart(), onStop(), and requestUpdate()
 * to react to subscriber events.
 */
struct BaseMonitor : public epics::pvAccess::Monitor
{
    POINTER_DEFINITIONS(BaseMonitor);
    weak_pointer weakself;
    inline shared_pointer shared_from_this() { return shared_pointer(weakself); }

    typedef epics::pvAccess::MonitorRequester requester_t;

    mutable epicsMutex lock; // not held during any callback
    typedef epicsGuard<epicsMutex> guard_t;

private:
    requester_t::shared_pointer requester;

    epics::pvData::PVStructurePtr complete;
    epics::pvData::BitSet changed, overflow;

    typedef std::deque<epics::pvAccess::MonitorElementPtr> buffer_t;
    bool inoverflow;
    bool running;
    size_t nbuffers;
    buffer_t inuse, empty;

public:
    BaseMonitor(const requester_t::shared_pointer& requester,
                const epics::pvData::PVStructure::shared_pointer& pvReq)
        :requester(requester)
        ,inoverflow(false)
        ,running(false)
        ,nbuffers(2)
    {}

    virtual ~BaseMonitor() {destroy();}

    inline const epics::pvData::PVStructurePtr& getValue() { return complete; }

    //! Must call before first post().  Sets .complete and calls monitorConnect()
    //! @note that value will never by accessed except by post() and requestUpdate()
    void connect(const epics::pvData::PVStructurePtr& value)
    {
        epics::pvData::StructureConstPtr dtype(value->getStructure());
        epics::pvData::PVDataCreatePtr create(epics::pvData::getPVDataCreate());
        BaseMonitor::shared_pointer self(shared_from_this());
        requester_t::shared_pointer req;
        {
            guard_t G(lock);
            assert(!complete); // can't call twice

            req = requester;

            complete = value;
            empty.resize(nbuffers);
            for(size_t i=0; i<empty.size(); i++) {
                empty[i].reset(new epics::pvAccess::MonitorElement(create->createPVStructure(dtype)));
            }
        }
        epics::pvData::Status sts;
        req->monitorConnect(sts, self, dtype);
    }

    struct no_overflow {};

    //! post update if queue not full, if full return false w/o overflow
    bool post(const epics::pvData::BitSet& updated, no_overflow)
    {
        requester_t::shared_pointer req;
        {
            guard_t G(lock);
            if(!complete || !running) return false;

            changed |= updated;

            if(empty.empty()) return false;

            if(p_postone())
                req = requester;
            inoverflow = false;
        }
        if(req) req->monitorEvent(shared_from_this());
        return true;
    }

    //! post update of pending changes.  eg. call from requestUpdate()
    bool post()
    {
        bool oflow;
        requester_t::shared_pointer req;
        {
            guard_t G(lock);
            if(!complete || !running) return false;

            if(empty.empty()) {
                oflow = inoverflow = true;

            } else {

                if(p_postone())
                    req = requester;
                oflow = inoverflow = false;
            }
        }
        if(req) req->monitorEvent(shared_from_this());
        return !oflow;
    }

    //! post update with changed and overflowed masks (eg. when updates were lost in some upstream queue)
    bool post(const epics::pvData::BitSet& updated, const epics::pvData::BitSet& overflowed)
    {
        bool oflow;
        requester_t::shared_pointer req;
        {
            guard_t G(lock);
            if(!complete || !running) return false;

            if(empty.empty()) {
                oflow = inoverflow = true;
                overflow |= overflowed;
                overflow.or_and(updated, changed);
                changed |= updated;

            } else {

                changed |= updated;
                if(p_postone())
                    req = requester;
                oflow = inoverflow = false;
            }
        }
        if(req) req->monitorEvent(shared_from_this());
        return !oflow;
    }

    //! post update with changed
    bool post(const epics::pvData::BitSet& updated) {
        bool oflow;
        requester_t::shared_pointer req;
        {
            guard_t G(lock);
            if(!complete || !running) return false;

            if(empty.empty()) {
                oflow = inoverflow = true;
                overflow.or_and(updated, changed);
                changed |= updated;

            } else {

                changed |= updated;
                if(p_postone())
                    req = requester;
                oflow = inoverflow = false;
            }
        }
        if(req) req->monitorEvent(shared_from_this());
        return !oflow;
    }

private:
    bool p_postone()
    {
        bool ret;
        // assume lock is held
        assert(!empty.empty());

        epics::pvAccess::MonitorElementPtr& elem = empty.front();

        elem->pvStructurePtr->copyUnchecked(*complete);
        *elem->changedBitSet = changed;
        *elem->overrunBitSet = overflow;

        overflow.clear();
        changed.clear();

        ret = inuse.empty();
        inuse.push_back(elem);
        empty.pop_front();
        return ret;
    }
public:

    // for special handling when MonitorRequester start()s or stop()s
    virtual void onStart() {}
    virtual void onStop() {}
    //! called when within release() when the opportunity exists to end the overflow condition
    //! May do nothing, or lock and call post()
    virtual void requestUpdate() {guard_t G(lock); post();}

    virtual void destroy()
    {
        requester_t::shared_pointer req;
        bool run;
        {
            guard_t G(lock);
            run = running;
            if(run) {
                running = false;
            }
            requester.swap(req);
        }
        if(run)
            this->onStop();
    }

private:
    virtual epics::pvData::Status start()
    {
        epics::pvData::Status ret;
        bool notify = false;
        BaseMonitor::shared_pointer self;
        {
            guard_t G(lock);
            if(running) return ret;
            running = true;
            if(!complete) return ret; // haveType() not called (error?)
            inoverflow = empty.empty();
            if(!inoverflow) {

                // post complete event
                overflow.clear();
                changed.clear();
                changed.set(0);
                notify = true;
            }
        }
        if(notify) onStart(); // may result in post()
        return ret;
    }

    virtual epics::pvData::Status stop()
    {
        BaseMonitor::shared_pointer self;
        bool notify;
        epics::pvData::Status ret;
        {
            guard_t G(lock);
            notify = running;
            running = false;
        }
        if(notify) onStop();
        return ret;
    }

    virtual epics::pvAccess::MonitorElementPtr poll()
    {
        epics::pvAccess::MonitorElementPtr ret;
        guard_t G(lock);
        if(running && complete && !inuse.empty()) {
            ret = inuse.front();
            inuse.pop_front();
        }
        return ret;
    }

    virtual void release(epics::pvAccess::MonitorElementPtr const & elem)
    {
        BaseMonitor::shared_pointer self;
        {
            guard_t G(lock);
            empty.push_back(elem);
            if(inoverflow)
                self = weakself.lock(); //TODO: concurrent release?
        }
        if(self)
            self->requestUpdate(); // may result in post()
    }
public:
    virtual void getStats(Stats& s) const
    {
        guard_t G(lock);
        s.nempty = empty.size();
        s.nfilled = inuse.size();
        s.noutstanding = nbuffers - s.nempty - s.nfilled;
    }
};

template<class CP>
struct BaseChannelProviderFactory : epics::pvAccess::ChannelProviderFactory
{
    typedef CP provider_type;
    std::string name;
    epicsMutex lock;
    std::tr1::weak_ptr<CP> last_shared;

    BaseChannelProviderFactory(const char *name) :name(name) {}
    virtual ~BaseChannelProviderFactory() {}

    virtual std::string getFactoryName() { return name; }

    virtual epics::pvAccess::ChannelProvider::shared_pointer sharedInstance()
    {
        epicsGuard<epicsMutex> G(lock);
        std::tr1::shared_ptr<CP> ret(last_shared.lock());
        if(!ret) {
            ret.reset(new CP());
            last_shared = ret;
        }
        return ret;
    }

    virtual epics::pvAccess::ChannelProvider::shared_pointer newInstance(const std::tr1::shared_ptr<epics::pvAccess::Configuration>&)
    {
        std::tr1::shared_ptr<CP> ret(new CP());
        return ret;
    }
};

#endif // PVAHELPER_H
