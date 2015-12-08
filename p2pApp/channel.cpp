
#include <epicsAtomic.h>

#define epicsExportSharedSymbols
#include "helper.h"
#include "pva2pva.h"
#include "channel.h"

namespace pva = epics::pvAccess;
namespace pvd = epics::pvData;

size_t GWChannel::num_instances;

GWChannel::GWChannel(ChannelCacheEntry::shared_pointer e,
                     pva::ChannelRequester::shared_pointer r)
    :entry(e)
    ,requester(r)
{
    epicsAtomicIncrSizeT(&num_instances);
}

GWChannel::~GWChannel()
{
    epicsAtomicDecrSizeT(&num_instances);
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

void
GWChannel::destroy()
{
    std::cout<<__PRETTY_FUNCTION__<<"\n";
    // Client closes channel. Release our references,
    // won't
    shared_pointer self(weakref);
    {
        Guard G(entry->cache->cacheLock);
        // remove ourselves before releasing our reference to prevent "stale" pointers.
        // Poke the cache so that this channel is held open for a while longer
        // in case this client reconnects shortly.
        entry->dropPoke = true;
        entry->interested.erase(self);
    }
    requester.reset();
    entry.reset();
}

std::tr1::shared_ptr<pva::ChannelProvider>
GWChannel::getProvider()
{
    return entry->cache->server.lock();
}

std::string
GWChannel::getRemoteAddress()
{
    // pass through address of origin server (information leak?)
    return entry->channel->getRemoteAddress();
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

std::tr1::shared_ptr<pva::ChannelRequester>
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
GWChannel::getField(pva::GetFieldRequester::shared_pointer const & requester,
                            std::string const & subField)
{
    //TODO: cache for top level field?
    std::cout<<"getField for "<<entry->channelName<<" '"<<subField<<"'\n";
    entry->channel->getField(requester, subField);
}

pva::AccessRights
GWChannel::getAccessRights(pvd::PVField::shared_pointer const & pvField)
{
    return entry->channel->getAccessRights(pvField);
}

pva::ChannelProcess::shared_pointer
GWChannel::createChannelProcess(
        pva::ChannelProcessRequester::shared_pointer const & channelProcessRequester,
        pvd::PVStructure::shared_pointer const & pvRequest)
{
    return entry->channel->createChannelProcess(channelProcessRequester, pvRequest);
}

pva::ChannelGet::shared_pointer
GWChannel::createChannelGet(
        pva::ChannelGetRequester::shared_pointer const & channelGetRequester,
        pvd::PVStructure::shared_pointer const & pvRequest)
{
    return entry->channel->createChannelGet(channelGetRequester, pvRequest);
}

pva::ChannelPut::shared_pointer
GWChannel::createChannelPut(
        pva::ChannelPutRequester::shared_pointer const & channelPutRequester,
        pvd::PVStructure::shared_pointer const & pvRequest)
{
    return entry->channel->createChannelPut(channelPutRequester, pvRequest);
}

pva::ChannelPutGet::shared_pointer
GWChannel::createChannelPutGet(
        pva::ChannelPutGetRequester::shared_pointer const & channelPutGetRequester,
        pvd::PVStructure::shared_pointer const & pvRequest)
{
    return entry->channel->createChannelPutGet(channelPutGetRequester, pvRequest);
}

pva::ChannelRPC::shared_pointer
GWChannel::createChannelRPC(
        pva::ChannelRPCRequester::shared_pointer const & channelRPCRequester,
        pvd::PVStructure::shared_pointer const & pvRequest)
{
    return entry->channel->createChannelRPC(channelRPCRequester, pvRequest);
}

namespace {
struct noclean {
    void operator()(MonitorCacheEntry *) {}
};
}

pvd::Monitor::shared_pointer
GWChannel::createMonitor(
        pvd::MonitorRequester::shared_pointer const & monitorRequester,
        pvd::PVStructure::shared_pointer const & pvRequest)
{
    std::cout<<__PRETTY_FUNCTION__<<"\n";
    ChannelCacheEntry::pvrequest_t ser;
    // serialize request struct to string using host byte order (only used for local comparison)
    pvd::serializeToVector(pvRequest.get(), EPICS_BYTE_ORDER, ser);

    MonitorCacheEntry::shared_pointer ent;
    MonitorUser::shared_pointer mon;

    pvd::Status startresult;
    pvd::StructureConstPtr typedesc;

    try {
        Guard G(entry->cache->cacheLock);

        ent = entry->mon_entries.find(ser);
        if(!ent) {
            ent.reset(new MonitorCacheEntry(entry.get()));
            entry->mon_entries[ser] = ent;

            // Create upstream monitor
            // This would create a strong ref. loop between ent and ent->mon.
            // Instead we get clever and pass a "fake" strong ref.
            // and ensure that ~MonitorCacheEntry destroy()s the client Monitor
            MonitorCacheEntry::shared_pointer fakereal(ent.get(), noclean());

            ent->mon = entry->channel->createMonitor(fakereal, pvRequest);

            ent->key.swap(ser); // no copy

            std::cout<<"Monitor cache "<<entry->channelName<<" Miss\n";
        } else {
            std::cout<<"Monitor cache "<<entry->channelName<<" Hit\n";
        }

        mon.reset(new MonitorUser(ent));
        ent->interested.insert(mon);
        mon->weakref = mon;
        mon->req = monitorRequester;
        typedesc = ent->typedesc;
        startresult = ent->startresult;

    } catch(std::exception& e) {
        mon.reset();
        std::cerr<<"Exception in "<<__PRETTY_FUNCTION__<<"\n"
                   "is "<<e.what()<<"\n";
        pvd::Status oops(pvd::Status::STATUSTYPE_FATAL, "Error during GWChannel setup");
        startresult = oops;
        monitorRequester->monitorConnect(oops, mon, typedesc);
        return mon;
    }

    // unlock for callback

    if(typedesc || !startresult.isSuccess()) {
        // upstream monitor already connected, or never will be,
        // so connect immeidately
        monitorRequester->monitorConnect(pvd::Status::Ok, mon, typedesc);
    }

    return mon;
}

pva::ChannelArray::shared_pointer
GWChannel::createChannelArray(
        pva::ChannelArrayRequester::shared_pointer const & channelArrayRequester,
        pvd::PVStructure::shared_pointer const & pvRequest)
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
