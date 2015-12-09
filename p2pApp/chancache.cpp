#include <stdio.h>

#include <epicsAtomic.h>

#include <pv/epicsException.h>
#include <pv/serverContext.h>

#define epicsExportSharedSymbols
#include "pva2pva.h"
#include "chancache.h"
#include "channel.h"

namespace pvd = epics::pvData;
namespace pva = epics::pvAccess;

size_t ChannelCacheEntry::num_instances;

ChannelCacheEntry::ChannelCacheEntry(ChannelCache* c, const std::string& n)
    :channelName(n), cache(c), dropPoke(true)
{
    epicsAtomicIncrSizeT(&num_instances);
}

ChannelCacheEntry::~ChannelCacheEntry()
{
    // Should *not* be holding cache->cacheLock
    std::cout<<"Destroy client channel for '"<<channelName<<"'\n";
    if(channel.get())
        channel->destroy(); // calls channelStateChange() w/ DESTROY
    epicsAtomicDecrSizeT(&num_instances);
}

std::string
ChannelCacheEntry::CRequester::getRequesterName()
{
    return "GWClient";
}

ChannelCacheEntry::CRequester::~CRequester() {}

void
ChannelCacheEntry::CRequester::message(std::string const & message, pvd::MessageType messageType)
{
    ChannelCacheEntry::shared_pointer chan(this->chan);
    if(chan)
        std::cout<<"message to client about '"<<chan->channelName<<"' : "<<message<<"\n";
}

// for ChannelRequester
void
ChannelCacheEntry::CRequester::channelCreated(const pvd::Status& status,
                            pva::Channel::shared_pointer const & channel)
{}

void
ChannelCacheEntry::CRequester::channelStateChange(pva::Channel::shared_pointer const & channel,
                                pva::Channel::ConnectionState connectionState)
{
    ChannelCacheEntry::shared_pointer chan(this->chan.lock());
    if(!chan)
        return;

    std::cout<<"Chan change '"<<chan->channelName<<"' is "
            <<pva::Channel::ConnectionStateNames[connectionState]<<"\n";


    ChannelCacheEntry::interested_t::vector_type interested;

    // fanout notification

    {
        Guard G(chan->cache->cacheLock);

        assert(chan->channel.get()==channel.get());

        switch(connectionState)
        {
        case pva::Channel::DISCONNECTED:
        case pva::Channel::DESTROYED:
            // Drop from cache
            chan->cache->entries.erase(chan->channelName);
            // keep 'chan' as a reference is that actual destruction doesn't happen which cacheLock is held
            break;
        default:
            break;
        }

        interested = chan->interested.lock_vector(); // Copy to allow unlock during callback
    }

    for(ChannelCacheEntry::interested_t::vector_type::const_iterator
        it=interested.begin(), end=interested.end();
        it!=end; ++it)
    {
        (*it)->requester->channelStateChange(*it, connectionState);
    }
}


struct ChannelCache::cacheClean : public epicsTimerNotify
{
    ChannelCache *cache;
    cacheClean(ChannelCache *c) : cache(c) {}
    epicsTimerNotify::expireStatus expire(const epicsTime &currentTime)
    {
        // keep a reference to any cache entrys being removed so they
        // aren't destroyed while cacheLock is held
        std::set<ChannelCacheEntry::shared_pointer> cleaned;

        {
            Guard G(cache->cacheLock);
            cache->cleanerRuns++;

            ChannelCache::entries_t::iterator cur=cache->entries.begin(), next, end=cache->entries.end();
            while(cur!=end) {
                next = cur;
                ++next;

                if(!cur->second->dropPoke && cur->second->interested.empty()) {
                    cleaned.insert(cur->second);
                    cache->entries.erase(cur);
                    cache->cleanerDust++;
                } else {
                    cur->second->dropPoke = false;
                }

                cur = next;
            }
        }
        return epicsTimerNotify::expireStatus(epicsTimerNotify::restart, 30.0);
    }
};

ChannelCache::ChannelCache()
    :provider(pva::getChannelProviderRegistry()->getProvider("pva"))
    ,timerQueue(&epicsTimerQueueActive::allocate(1, epicsThreadPriorityCAServerLow-2))
    ,cleaner(new cacheClean(this))
{
    if(!provider)
        throw std::logic_error("Missing 'pva' provider");
    assert(timerQueue);
    cleanTimer = &timerQueue->createTimer();
    cleanTimer->start(*cleaner, 30.0);
}

ChannelCache::~ChannelCache()
{
    cleanTimer->destroy();
    timerQueue->release();
    delete cleaner;
}
