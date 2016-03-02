#ifndef PVAHELPER_H
#define PVAHELPER_H

#include <epicsGuard.h>

#include <pv/pvAccess.h>

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
    epics::pvAccess::ChannelRequester::shared_pointer requester;
    const epics::pvData::StructureConstPtr fielddesc;

    // assume Requester methods not called after destory()
    virtual std::string getRequesterName() { guard_t G(lock); return requester->getRequesterName(); }

    virtual void destroy() { guard_t G(lock); provider.reset(); requester.reset(); }

    virtual std::tr1::shared_ptr<epics::pvAccess::ChannelProvider> getProvider() { guard_t G(lock); return provider; }
    virtual std::string getRemoteAddress() { guard_t G(lock); return requester->getRequesterName(); }
    virtual ConnectionState getConnectionState() { return epics::pvAccess::Channel::CONNECTED; }
    virtual std::string getChannelName() { return pvname; }
    virtual std::tr1::shared_ptr<epics::pvAccess::ChannelRequester> getChannelRequester() { guard_t G(lock); return requester; }
    virtual bool isConnected() { return getConnectionState()==epics::pvAccess::Channel::CONNECTED; }

    virtual void getField(epics::pvAccess::GetFieldRequester::shared_pointer const & requester,std::string const & subField)
    { requester->getDone(epics::pvData::Status(), fielddesc); }


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

#endif // PVAHELPER_H
