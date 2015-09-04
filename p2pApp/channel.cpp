
#define epicsExportSharedSymbols
#include "pva2pva.h"
#include "channel.h"

namespace pva = epics::pvAccess;
namespace pvd = epics::pvData;

GWChannel::GWChannel(ChannelCacheEntry::shared_pointer e,
                     epics::pvAccess::ChannelRequester::shared_pointer r)
    :entry(e)
    ,requester(r)
{
    Guard G(entry->cache->cacheLock);
    entry->interested.insert(this);
}

GWChannel::~GWChannel()
{
    Guard G(entry->cache->cacheLock);
    entry->interested.erase(this);
    std::cout<<"GWChannel dtor '"<<entry->channelName<<"'\n";
}

std::string
GWChannel::getRequesterName()
{
    return "GWChannel";
}

void
GWChannel::message(std::string const & message, pvd::MessageType messageType)
{
    std::cout<<"message to client about '"<<entry->channelName<<"' : "<<message<<"\n";
}

std::tr1::shared_ptr<epics::pvAccess::ChannelProvider>
GWChannel::getProvider()
{
    return entry->cache->server;
}

std::string
GWChannel::getRemoteAddress()
{
    return "foobar";
}

pva::Channel::ConnectionState
GWChannel::getConnectionState()
{
    return entry->channel->getConnectionState();
}

std::string
GWChannel::getChannelName()
{
    return entry->channelName;
}

std::tr1::shared_ptr<epics::pvAccess::ChannelRequester>
GWChannel::getChannelRequester()
{
    return requester;
}

bool
GWChannel::isConnected()
{
    return entry->channel->isConnected();
}


void
GWChannel::getField(epics::pvAccess::GetFieldRequester::shared_pointer const & requester,
                            std::string const & subField)
{
    //TODO: cache for top level field?
    std::cout<<"getField for "<<entry->channelName<<" "<<subField<<"\n";
    entry->channel->getField(requester, subField);
}

epics::pvAccess::AccessRights
GWChannel::getAccessRights(epics::pvData::PVField::shared_pointer const & pvField)
{
    return entry->channel->getAccessRights(pvField);
}

epics::pvAccess::ChannelProcess::shared_pointer
GWChannel::createChannelProcess(
        epics::pvAccess::ChannelProcessRequester::shared_pointer const & channelProcessRequester,
        epics::pvData::PVStructure::shared_pointer const & pvRequest)
{
    return entry->channel->createChannelProcess(channelProcessRequester, pvRequest);
}

epics::pvAccess::ChannelGet::shared_pointer
GWChannel::createChannelGet(
        epics::pvAccess::ChannelGetRequester::shared_pointer const & channelGetRequester,
        epics::pvData::PVStructure::shared_pointer const & pvRequest)
{
    return entry->channel->createChannelGet(channelGetRequester, pvRequest);
}

epics::pvAccess::ChannelPut::shared_pointer
GWChannel::createChannelPut(
        epics::pvAccess::ChannelPutRequester::shared_pointer const & channelPutRequester,
        epics::pvData::PVStructure::shared_pointer const & pvRequest)
{
    return entry->channel->createChannelPut(channelPutRequester, pvRequest);
}

epics::pvAccess::ChannelPutGet::shared_pointer
GWChannel::createChannelPutGet(
        epics::pvAccess::ChannelPutGetRequester::shared_pointer const & channelPutGetRequester,
        epics::pvData::PVStructure::shared_pointer const & pvRequest)
{
    return entry->channel->createChannelPutGet(channelPutGetRequester, pvRequest);
}

epics::pvAccess::ChannelRPC::shared_pointer
GWChannel::createChannelRPC(
        epics::pvAccess::ChannelRPCRequester::shared_pointer const & channelRPCRequester,
        epics::pvData::PVStructure::shared_pointer const & pvRequest)
{
    return entry->channel->createChannelRPC(channelRPCRequester, pvRequest);
}

epics::pvData::Monitor::shared_pointer
GWChannel::createMonitor(
        epics::pvData::MonitorRequester::shared_pointer const & monitorRequester,
        epics::pvData::PVStructure::shared_pointer const & pvRequest)
{
    //TODO de-dup monitors
    return entry->channel->createMonitor(monitorRequester, pvRequest);
}

epics::pvAccess::ChannelArray::shared_pointer
GWChannel::createChannelArray(
        epics::pvAccess::ChannelArrayRequester::shared_pointer const & channelArrayRequester,
        epics::pvData::PVStructure::shared_pointer const & pvRequest)
{
    return entry->channel->createChannelArray(channelArrayRequester, pvRequest);
}


void
GWChannel::printInfo()
{ printInfo(std::cout); }
void
GWChannel::printInfo(std::ostream& out)
{
    out<<"GWChannel for "<<entry->channelName<<"\n";
}
