
#include <epicsAtomic.h>

#define epicsExportSharedSymbols
#include "helper.h"
#include "iocshelper.h"
#include "pva2pva.h"
#include "channel.h"

namespace pva = epics::pvAccess;
namespace pvd = epics::pvData;

int p2pReadOnly = 0;

size_t GWChannel::num_instances;

GWChannel::GWChannel(const ChannelCacheEntry::shared_pointer& e,
                     const epics::pvAccess::ChannelProvider::weak_pointer& srvprov,
                     const pva::ChannelRequester::shared_pointer& r,
                     const std::string& addr)
    :entry(e)
    ,requester(r)
    ,address(addr)
    ,server_provder(srvprov)
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
    epics::pvAccess::ChannelRequester::shared_pointer req;
    {
        Guard G(entry->mutex());
        req.swap(requester);
    }
}

std::tr1::shared_ptr<pva::ChannelProvider>
GWChannel::getProvider()
{
    return server_provder.lock();
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
    if(!p2pReadOnly)
        return entry->channel->createChannelProcess(channelProcessRequester, pvRequest);
    else
        return Channel::createChannelProcess(channelProcessRequester, pvRequest);
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
    if(!p2pReadOnly)
        return entry->channel->createChannelPut(channelPutRequester, pvRequest);
    else
        return Channel::createChannelPut(channelPutRequester, pvRequest);
}

pva::ChannelPutGet::shared_pointer
GWChannel::createChannelPutGet(
        pva::ChannelPutGetRequester::shared_pointer const & channelPutGetRequester,
        pvd::PVStructure::shared_pointer const & pvRequest)
{
    if(!p2pReadOnly)
        return entry->channel->createChannelPutGet(channelPutGetRequester, pvRequest);
    else
        return Channel::createChannelPutGet(channelPutGetRequester, pvRequest);
}

pva::ChannelRPC::shared_pointer
GWChannel::createChannelRPC(
        pva::ChannelRPCRequester::shared_pointer const & channelRPCRequester,
        pvd::PVStructure::shared_pointer const & pvRequest)
{
    if(!p2pReadOnly)
        return entry->channel->createChannelRPC(channelRPCRequester, pvRequest);
    else
        return Channel::createChannelRPC(channelRPCRequester, pvRequest);
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

    MonitorCacheEntry::shared_pointer ment;
    MonitorUser::shared_pointer mon;

    pvd::Status startresult;
    pvd::StructureConstPtr typedesc;

    try {
        {
            Guard G(entry->mutex());

            // TODO: no-cache/no-share flag in pvRequest

            ment = entry->mon_entries.find(ser);
            if(!ment) {
                ment.reset(new MonitorCacheEntry(entry.get(), pvRequest));
                entry->mon_entries[ser] = ment; // ref. wrapped
                ment->weakref = ment;

                // We've added an incomplete entry (no Monitor)
                // so MonitorUser must check validity before de-ref.
                // in this case we use !!typedesc as this also indicates
                // that the upstream monitor is connected
                pvd::MonitorPtr M;
                {
                    UnGuard U(G);

                    // Create upstream monitor
                    // This would create a strong ref. loop between ent and ent->mon.
                    // Instead we get clever and pass a "fake" strong ref.
                    // and ensure that ~MonitorCacheEntry destroy()s the client Monitor
                    MonitorCacheEntry::shared_pointer fakereal(ment.get(), noclean());

                    M = entry->channel->createMonitor(fakereal, pvRequest);
                }
                ment->mon = M;

                std::cout<<"Monitor cache "<<entry->channelName<<" Miss\n";
            } else {
                std::cout<<"Monitor cache "<<entry->channelName<<" Hit\n";
            }
        }

        Guard G(ment->mutex());

        mon.reset(new MonitorUser(ment));
        ment->interested.insert(mon);
        mon->weakref = mon;
        mon->srvchan = shared_pointer(weakref);
        mon->req = monitorRequester;

        typedesc = ment->typedesc;
        startresult = ment->startresult;

    } catch(std::exception& e) {
        mon.reset();
        std::cerr<<"Exception in "<<__PRETTY_FUNCTION__<<"\n"
                   "is "<<e.what()<<"\n";
        startresult = pvd::Status(pvd::Status::STATUSTYPE_FATAL, "Error during GWChannel setup");
    }

    // unlock for callback

    if(typedesc || !startresult.isSuccess()) {
        // upstream monitor already connected, or never will be.
        monitorRequester->monitorConnect(startresult, mon, typedesc);
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


void registerReadOnly()
{
    iocshVariable<int, &p2pReadOnly>("p2pReadOnly");
}
