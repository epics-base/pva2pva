#ifndef CHANCACHE_H
#define CHANCACHE_H

#include <string>
#include <map>
#include <set>
#include <deque>

#include <epicsMutex.h>
#include <epicsTimer.h>

#include <pv/pvAccess.h>

#include "weakmap.h"
#include "weakset.h"

struct ChannelCache;
struct ChannelCacheEntry;
struct MonitorUser;
struct GWChannel;

struct MonitorCacheEntry : public epics::pvData::MonitorRequester
{
    POINTER_DEFINITIONS(MonitorCacheEntry);
    static size_t num_instances;
    weak_pointer weakref;

    ChannelCacheEntry * const chan;

    // to avoid yet another mutex borrow interested.mutex() for our members
    inline epicsMutex& mutex() const { return interested.mutex(); }

    typedef std::vector<epicsUInt8> pvrequest_t;

    bool done;
    size_t nwakeups; // # of upstream monitorEvent() calls
    size_t nevents;  // # of upstream events poll()'d

    epics::pvData::StructureConstPtr typedesc;
    epics::pvData::PVStructure::shared_pointer lastval;
    epics::pvData::MonitorPtr mon;
    epics::pvData::Status startresult;

    typedef weak_set<MonitorUser> interested_t;
    interested_t interested;

    MonitorCacheEntry(ChannelCacheEntry *ent);
    virtual ~MonitorCacheEntry();

    virtual void monitorConnect(epics::pvData::Status const & status,
                                epics::pvData::MonitorPtr const & monitor,
                                epics::pvData::StructureConstPtr const & structure);
    virtual void monitorEvent(epics::pvData::MonitorPtr const & monitor);
    virtual void unlisten(epics::pvData::MonitorPtr const & monitor);

    virtual std::string getRequesterName();
    virtual void message(std::string const & message, epics::pvData::MessageType messageType);
};

struct MonitorUser : public epics::pvData::Monitor
{
    POINTER_DEFINITIONS(MonitorUser);
    static size_t num_instances;
    weak_pointer weakref;

    MonitorCacheEntry::shared_pointer entry;
    epics::pvData::MonitorRequester::weak_pointer req;

    // guards queues and member variables
    epicsMutex queueLock;
    bool running;
    size_t nwakeups; // # of monitorEvent() calls to req
    size_t nevents;  // total # events queued
    size_t ndropped; // # of events drop because our queue was full

    std::deque<epics::pvData::MonitorElementPtr> filled, empty;
    std::set<epics::pvData::MonitorElementPtr> inuse;

    MonitorUser(const MonitorCacheEntry::shared_pointer&);
    virtual ~MonitorUser();

    virtual void destroy();

    virtual epics::pvData::Status start();
    virtual epics::pvData::Status stop();
    virtual epics::pvData::MonitorElementPtr poll();
    virtual void release(epics::pvData::MonitorElementPtr const & monitorElement);

    virtual std::string getRequesterName();
    virtual void message(std::string const & message, epics::pvData::MessageType messageType);
};

struct ChannelCacheEntry
{
    POINTER_DEFINITIONS(ChannelCacheEntry);
    static size_t num_instances;

    const std::string channelName;
    ChannelCache * const cache;

    // to avoid yet another mutex borrow interested.mutex() for our members
    inline epicsMutex& mutex() const { return interested.mutex(); }

    // clientChannel
    epics::pvAccess::Channel::shared_pointer channel;

    bool dropPoke;

    typedef weak_set<GWChannel> interested_t;
    interested_t interested;

    typedef MonitorCacheEntry::pvrequest_t pvrequest_t;
    typedef weak_value_map<pvrequest_t, MonitorCacheEntry> mon_entries_t;
    mon_entries_t mon_entries;

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
    size_t cleanerRuns;
    size_t cleanerDust;

    ChannelCache();
    ~ChannelCache();
};

#endif // CHANCACHE_H

