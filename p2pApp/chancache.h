#ifndef CHANCACHE_H
#define CHANCACHE_H

#include <string>
#include <map>
#include <set>

#include <epicsMutex.h>
#include <epicsTimer.h>

#include <pv/pvAccess.h>

struct ChannelCache;
struct GWChannel;

struct ChannelCacheEntry
{
    POINTER_DEFINITIONS(ChannelCacheEntry);

    struct Update {
        virtual ~Update()=0;
        virtual void channelStateChange(epics::pvAccess::Channel::ConnectionState connectionState) = 0;
    };

    const std::string channelName;
    ChannelCache * const cache;

    // clientChannel
    epics::pvAccess::Channel::shared_pointer channel;

    bool dropPoke;

    typedef std::set<GWChannel*> interested_t;
    interested_t interested;

    ChannelCacheEntry(ChannelCache*, const std::string& n);
    virtual ~ChannelCacheEntry();

    // this exists as a seperate object to prevent a reference loop
    // ChannelCacheEntry -> pva::Channel -> CRequester
    struct CRequester : public epics::pvAccess::ChannelRequester
    {
        CRequester(const ChannelCacheEntry::shared_pointer& p) : chan(p) {}
        virtual ~CRequester();
        ChannelCacheEntry::weak_pointer chan;
        // for Requester
        virtual std::string getRequesterName();
        virtual void message(std::string const & message, epics::pvData::MessageType messageType);
        // for ChannelRequester
        virtual void channelCreated(const epics::pvData::Status& status,
                                    epics::pvAccess::Channel::shared_pointer const & channel);
        virtual void channelStateChange(epics::pvAccess::Channel::shared_pointer const & channel,
                                        epics::pvAccess::Channel::ConnectionState connectionState);
    };
};

/** Holds the set of channels the GW is searching for, or has found.
 */
struct ChannelCache
{
    typedef std::map<std::string, ChannelCacheEntry::shared_pointer > entries_t;

    // cacheLock should not be held while calling *Requester methods
    epicsMutex cacheLock;

    entries_t entries;

    epics::pvAccess::ChannelProvider::shared_pointer provider; // client Provider
    epics::pvAccess::ChannelProvider::weak_pointer server; // GWServerChannelProvider

    epicsTimerQueueActive *timerQueue;
    epicsTimer *cleanTimer;
    struct cacheClean;
    cacheClean *cleaner;

    ChannelCache();
    ~ChannelCache();

    // caller must host cacheLock
    ChannelCacheEntry::shared_pointer get(const std::string& name);

};

#endif // CHANCACHE_H

