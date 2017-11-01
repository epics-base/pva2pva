#include <stdio.h>

#include <epicsAtomic.h>
#include <epicsString.h>

#include <pv/pvIntrospect.h> /* for pvdVersion.h */
#include <pv/epicsException.h>
#include <pv/serverContext.h>
#include <pv/logger.h>

#define epicsExportSharedSymbols
#include "helper.h"
#include "iocshelper.h"
#include "pva2pva.h"
#include "server.h"

#if defined(PVDATA_VERSION_INT)
#if PVDATA_VERSION_INT > VERSION_INT(7,0,0,0)
#  define USE_MSTATS
#endif
#endif

namespace pva = epics::pvAccess;
namespace pvd = epics::pvData;

std::tr1::shared_ptr<pva::ChannelProvider>
GWServerChannelProvider::getChannelProvider()
{
    return shared_from_this();
}

// Called from UDP search thread with no locks held
// Called from TCP threads (for search w/ TCP)
pva::ChannelFind::shared_pointer
GWServerChannelProvider::channelFind(std::string const & channelName,
                                     pva::ChannelFindRequester::shared_pointer const & channelFindRequester)
{
    pva::ChannelFind::shared_pointer ret;
    bool found = false;

    if(!channelName.empty())
    {
        std::string newName;

        // rewrite name
        newName = channelName;
        //newName[0] = 'y';


        ChannelCacheEntry::shared_pointer ent(cache.lookup(newName));
        if(ent) {
            found = true;
            ret = shared_from_this();
        }
    }

    // unlock for callback

    channelFindRequester->channelFindResult(pvd::Status::Ok, ret, found);

    return ret;
}

// The return value of this function is ignored
// The newly created channel is given to the ChannelRequester
pva::Channel::shared_pointer
GWServerChannelProvider::createChannel(std::string const & channelName,
                                       pva::ChannelRequester::shared_pointer const & channelRequester,
                                       short priority, std::string const & addressx)
{
    GWChannel::shared_pointer ret;
    std::string newName;
    std::string address = channelRequester->getRequesterName();

    if(!channelName.empty())
    {

        // rewrite name
        newName = channelName;
        //newName[0] = 'y';

        Guard G(cache.cacheLock);

        ChannelCacheEntry::shared_pointer ent(cache.lookup(newName)); // recursively locks cacheLock

        if(ent)
        {
            ret.reset(new GWChannel(ent, shared_from_this(), channelRequester, address));
            ent->interested.insert(ret);
            ret->weakref = ret;
        }
    }

    if(!ret) {
        std::cerr<<"GWServer refusing channel "<<channelName<<"\n";
        pvd::Status S(pvd::Status::STATUSTYPE_ERROR, "Not found");
        channelRequester->channelCreated(S, ret);
    } else {
        std::cerr<<"GWServer connecting channel "<<channelName<<" as "<<newName<<"\n";
        channelRequester->channelCreated(pvd::Status::Ok, ret);
        channelRequester->channelStateChange(ret, pva::Channel::CONNECTED);
    }

    return ret; // ignored by caller
}

void GWServerChannelProvider::destroy()
{
    std::cout<<"GWServer destory request\n";
}

GWServerChannelProvider::GWServerChannelProvider(const pva::ChannelProvider::shared_pointer& prov)
    :cache(prov)
{
    std::cout<<"GW Server ctor\n";
}
GWServerChannelProvider::~GWServerChannelProvider()
{
    std::cout<<"GW Server dtor\n";
}

namespace {

static epicsMutex gbllock;
static std::tr1::shared_ptr<pva::ServerContext> gblctx;

void startServer()
{
    try {
        Guard G(gbllock);
        if(gblctx) {
            printf("Already started\n");
            return;
        }

        pva::ChannelProvider::shared_pointer client(pva::ChannelProviderRegistry::clients()->getProvider("pva"));
        GWServerChannelProvider::shared_pointer server(new GWServerChannelProvider(client));

        gblctx = pva::ServerContext::create(pva::ServerContext::Config()
                                            .provider(server)
                                            .config(pva::ConfigurationBuilder()
                                                    .push_env()
                                                    .build()));
    }catch(std::exception& e){
        printf("Error: %s\n", e.what());
    }
}

void stopServer()
{
    try {
        Guard G(gbllock);

        if(!gblctx) {
            printf("Not started\n");
            return;
        } else {
            gblctx->shutdown();
            gblctx.reset();
        }
    }catch(std::exception& e){
        printf("Error: %s\n", e.what());
    }
}

void infoServer(int lvl)
{
    try {
        Guard G(gbllock);

        if(gblctx) {
            gblctx->printInfo();
        } else {
            printf("Not running");
        }
    }catch(std::exception& e){
        printf("Error: %s\n", e.what());
    }
}

void statusServer(int lvl, const char *chanexpr)
{
    try{
        pva::ServerContext::shared_pointer ctx;
        {
            Guard G(gbllock);
            ctx = gblctx;
        }
        if(!ctx) {
            std::cout<<"Not running\n";
            return;
        }

        bool iswild = chanexpr ? (strchr(chanexpr, '?') || strchr(chanexpr, '*')) : false;
        if(chanexpr && lvl<1)
            lvl=1; // giving a channel implies at least channel level of detail

        const std::vector<pva::ChannelProvider::shared_pointer>& prov(ctx->getChannelProviders());

        std::cout<<"Server has "<<prov.size()<<" providers\n";
        for(size_t i=0; i<prov.size(); i++)
        {
            pva::ChannelProvider* p = prov[i].get();
            std::cout<<"Provider: "<<(p ? p->getProviderName() : std::string("NULL"))<<"\n";
            if(!p) continue;
            GWServerChannelProvider *scp = dynamic_cast<GWServerChannelProvider*>(p);
            if(!scp) continue;

            ChannelCache::entries_t entries;

            size_t ncache, ncleaned, ndust;
            {
                Guard G(scp->cache.cacheLock);

                ncache = scp->cache.entries.size();
                ncleaned = scp->cache.cleanerRuns;
                ndust = scp->cache.cleanerDust;

                if(lvl>0) {
                    if(!chanexpr || iswild) { // no string or some glob pattern
                        entries = scp->cache.entries; // copy of std::map
                    } else if(chanexpr) { // just one channel
                        ChannelCache::entries_t::iterator it(scp->cache.entries.find(chanexpr));
                        if(it!=scp->cache.entries.end())
                            entries[it->first] = it->second;
                    }
                }
            }

            std::cout<<"Cache has "<<ncache<<" channels.  Cleaned "
                    <<ncleaned<<" times closing "<<ndust<<" channels\n";

            if(lvl<=0)
                continue;

            FOREACH(ChannelCache::entries_t::const_iterator, it, end, entries) {
                const std::string& channame = it->first;
                if(iswild && !epicsStrGlobMatch(channame.c_str(), chanexpr))
                    continue;

                ChannelCacheEntry& E = *it->second;
                ChannelCacheEntry::mon_entries_t::lock_vector_type mons;
                size_t nsrv, nmon;
                bool dropflag;
                const char *chstate;
                {
                    Guard G(E.mutex());
                    chstate = pva::Channel::ConnectionStateNames[E.channel->getConnectionState()];
                    nsrv = E.interested.size();
                    nmon = E.mon_entries.size();
                    dropflag = E.dropPoke;

                    if(lvl>1)
                        mons = E.mon_entries.lock_vector();
                }

                std::cout<<chstate
                         <<" Client Channel '"<<channame
                         <<"' used by "<<nsrv<<" Server channel(s) with "
                         <<nmon<<" unique subscription(s) "
                         <<(dropflag?'!':'_')<<"\n";

                if(lvl<=1)
                    continue;

                FOREACH(ChannelCacheEntry::mon_entries_t::lock_vector_type::const_iterator, it2, end2, mons) {
                    MonitorCacheEntry& ME =  *it2->second;

                    MonitorCacheEntry::interested_t::vector_type usrs;
                    size_t nsrvmon;
#ifdef USE_MSTATS
                    pvd::Monitor::Stats mstats;
#endif
                    bool hastype, hasdata, isdone;
                    {
                        Guard G(ME.mutex());

                        nsrvmon = ME.interested.size();
                        hastype = !!ME.typedesc;
                        hasdata = !!ME.lastelem;
                        isdone = ME.done;

#ifdef USE_MSTATS
                        if(ME.mon)
                            ME.mon->getStats(mstats);
#endif

                        if(lvl>2)
                            usrs = ME.interested.lock_vector();
                    }

                    // TODO: how to describe pvRequest in a compact way...
                    std::cout<<"  Client Monitor used by "<<nsrvmon<<" Server monitors, "
                             <<"Has "<<(hastype?"":"not ")
                             <<"opened, Has "<<(hasdata?"":"not ")
                             <<"recv'd some data, Has "<<(isdone?"":"not ")<<"finalized\n"
                               "    "<<      epicsAtomicGetSizeT(&ME.nwakeups)<<" wakeups "
                             <<epicsAtomicGetSizeT(&ME.nevents)<<" events\n";
#ifdef USE_MSTATS
                    if(mstats.nempty || mstats.nfilled || mstats.noutstanding)
                        std::cout<<"    US monitor queue "<<mstats.nfilled
                                 <<" filled, "<<mstats.noutstanding
                                 <<" outstanding, "<<mstats.nempty<<" empty\n";
#endif
                    if(lvl<=2)
                        continue;

                    FOREACH(MonitorCacheEntry::interested_t::vector_type::const_iterator, it3, end3, usrs) {
                        MonitorUser& MU = **it3;

                        size_t nempty, nfilled, nused, total;
                        std::string remote;
                        bool isrunning;
                        {
                            Guard G(MU.mutex());

                            nempty = MU.empty.size();
                            nfilled = MU.filled.size();
                            nused = MU.inuse.size();
                            isrunning = MU.running;

                            GWChannel::shared_pointer srvchan(MU.srvchan.lock());
                            if(srvchan)
                                remote = srvchan->address;
                            else
                                remote = "<unknown>";
                        }
                        total = nempty + nfilled + nused;

                        std::cout<<"    Server monitor from "
                                 <<remote
                                 <<(isrunning?"":" Paused")
                                 <<" buffer "<<nfilled<<"/"<<total
                                 <<" out "<<nused<<"/"<<total
                                 <<" "<<epicsAtomicGetSizeT(&MU.nwakeups)<<" wakeups "
                                 <<epicsAtomicGetSizeT(&MU.nevents)<<" events "
                                 <<epicsAtomicGetSizeT(&MU.ndropped)<<" drops\n";
                    }
                }
            }
        }

    }catch(std::exception& e){
        std::cerr<<"Error: "<<e.what()<<"\n";
    }
}

void dropChannel(const char *chan)
{
    if(!chan) return;
    try {
        pva::ServerContext::shared_pointer ctx(gblctx);
        if(!ctx) {
            std::cout<<"Not running\n";
            return;
        }
        std::cout<<"Find and force drop channel '"<<chan<<"'\n";

        const std::vector<pva::ChannelProvider::shared_pointer>& prov(ctx->getChannelProviders());

        for(size_t i=0; i<prov.size(); i++)
        {
            pva::ChannelProvider* p = prov[i].get();
            if(!p) continue;
            GWServerChannelProvider *scp = dynamic_cast<GWServerChannelProvider*>(p);
            if(!scp) continue;

            ChannelCacheEntry::shared_pointer entry;

            // find the channel, if it's there
            {
                Guard G(scp->cache.cacheLock);

                ChannelCache::entries_t::iterator it = scp->cache.entries.find(chan);
                if(it==scp->cache.entries.end())
                    continue;

                std::cout<<"Found in provider "<<p->getProviderName()<<"\n";

                entry = it->second;
                scp->cache.entries.erase(it); // drop out of cache (TODO: not required)
            }

            // trigger client side disconnect (recursively calls call CRequester::channelStateChange())
            entry->channel->destroy();
        }

        std::cout<<"Done\n";
    }catch(std::exception& e){
        std::cerr<<"Error: "<<e.what()<<"\n";
    }
}

void show_cnt(const char *name, const size_t& live, const size_t& reach) {
    ptrdiff_t delta = ptrdiff_t(live)-ptrdiff_t(reach);
    std::cout<<name<<" live: "<<live
            <<" reachable: "<<reach
            <<" delta: "<<delta<<"\n";
}

void refCheck(int lvl)
{
    try{
        std::cout<<"GW instances reference counts.\n"
                   "Note: small inconsistencies (positive and negative) are normal due to locking.\n"
                   "      Actual leaks will manifest as a sustained increases.\n";

        size_t chan_count = 0, mon_count = 0, mon_user_count = 0;
        pva::ServerContext::shared_pointer ctx(gblctx);
        if(!ctx) {
            std::cout<<"Not running\n";
            return;
        }
        if(ctx) {
            const std::vector<pva::ChannelProvider::shared_pointer>& prov(ctx->getChannelProviders());

            if(lvl>0) std::cout<<"Server has "<<prov.size()<<" providers\n";

            for(size_t i=0; i<prov.size(); i++)
            {
                pva::ChannelProvider* p = prov[i].get();
                if(!p) continue;
                GWServerChannelProvider *scp = dynamic_cast<GWServerChannelProvider*>(p);
                if(!scp) continue;

                ChannelCache::entries_t entries;
                {
                    Guard G(scp->cache.cacheLock);
                    entries = scp->cache.entries; // Copy
                }

                if(lvl>0) std::cout<<" Cache has "<<entries.size()<<" channels\n";

                chan_count += entries.size();

                FOREACH(ChannelCache::entries_t::const_iterator, it, end, entries)
                {
                    ChannelCacheEntry::mon_entries_t::lock_vector_type M(it->second->mon_entries.lock_vector());

                    if(lvl>0) std::cout<<"  Channel "<<it->second->channelName
                                      <<" has "<<M.size()<<" Client Monitors\n";

                    mon_count += M.size();
                    FOREACH(ChannelCacheEntry::mon_entries_t::lock_vector_type::const_iterator, it2, end2, M)
                    {
                        const MonitorCacheEntry::interested_t& W(it2->second->interested);
                        if(lvl>0) std::cout<<"   Used by "<<W.size()<<" Client Monitors\n";
                        mon_user_count += W.size();
                    }
                }
            }
        } else {
            std::cout<<"Server Not running\n";
        }

        size_t chan_latch = epicsAtomicGetSizeT(&ChannelCacheEntry::num_instances),
               mon_latch = epicsAtomicGetSizeT(&MonitorCacheEntry::num_instances),
               mon_user_latch = epicsAtomicGetSizeT(&MonitorUser::num_instances);

        std::cout<<"GWChannel live: "<<epicsAtomicGetSizeT(&GWChannel::num_instances)<<"\n";
        show_cnt("ChannelCacheEntry",chan_latch,chan_count);
        show_cnt("MonitorCacheEntry",mon_latch,mon_count);
        show_cnt("MonitorUser",mon_user_latch,mon_user_count);

    }catch(std::exception& e){
        std::cerr<<"Error: "<<e.what()<<"\n";
    }
}

void pvadebug(const char *lvl)
{
    try {
        std::string lname(lvl ? lvl : "warn");
        pva::pvAccessLogLevel elvl;
        if(lname=="off" || lname=="-2")
            elvl = pva::logLevelOff;
        else if(lname=="fatal" || lname=="-1")
            elvl = pva::logLevelFatal;
        else if(lname=="error" || lname=="0")
            elvl = pva::logLevelError;
        else if(lname=="warn" || lname=="1")
            elvl = pva::logLevelWarn;
        else if(lname=="info" || lname=="2")
            elvl = pva::logLevelInfo;
        else if(lname=="debug" || lname=="3")
            elvl = pva::logLevelDebug;
        else if(lname=="trace" || lname=="4")
            elvl = pva::logLevelTrace;
        else if(lname=="all" || lname=="5")
            elvl = pva::logLevelAll;
        else
            throw std::invalid_argument("Unknown level name, must be one of off|fatal|error|warn|info|debug|trace|all");

        pva::pvAccessSetLogLevel(elvl);
    }catch(std::exception& e){
        std::cout<<"Error: "<<e.what()<<"\n";
    }
}

} // namespace

void registerGWServerIocsh()
{
    iocshRegister<&startServer>("gwstart");
    iocshRegister<&stopServer>("gwstop");
    iocshRegister<int, &infoServer>("pvasr", "level");
    iocshRegister<int, const char*, &statusServer>("gwstatus", "level", "channel name/pattern");
    iocshRegister<const char*, &dropChannel>("gwdrop", "channel");
    iocshRegister<int, &refCheck>("gwref", "level");
    iocshRegister<const char*, &pvadebug>("pvadebug", "level");
}

void gwServerShutdown()
{
    stopServer();
}
