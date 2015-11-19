#include <stdio.h>

#include <epicsAtomic.h>

#include <pv/epicsException.h>
#include <pv/serverContext.h>

#define epicsExportSharedSymbols
#include "helper.h"
#include "pva2pva.h"
#include "iocshelper.h"
#include "chancache.h"
#include "channel.h"

namespace pva = epics::pvAccess;
namespace pvd = epics::pvData;

namespace {

struct GWServerChannelProvider : public
        pva::ChannelProvider,
        pva::ChannelFind,
        std::tr1::enable_shared_from_this<GWServerChannelProvider>
{
    ChannelCache cache;

    // for ChannelFind

    virtual std::tr1::shared_ptr<ChannelProvider> getChannelProvider()
    {
        return this->shared_from_this();
    }

    virtual void cancel() {}

    // For ChannelProvider

    virtual std::string getProviderName() {
        return "GWServer";
    }

    // Called from UDP search thread with no locks held
    // Called from TCP threads (for search w/ TCP)
    virtual pva::ChannelFind::shared_pointer channelFind(std::string const & channelName,
                                             pva::ChannelFindRequester::shared_pointer const & channelFindRequester)
    {
        pva::ChannelFind::shared_pointer ret;
        bool found = false;

        // TODO
        // until GW can bind client and server to specific (and different) interfaces
        // use a naming convension to avoid loops (GW talks to itself).
        // Server listens for names beginning with 'x',
        // and re-writes these to start with 'y' for client search.
        if(!channelName.empty() && channelName[0]=='x')
        {
            std::string newName;

            // rewrite name
            newName = channelName;
            newName[0] = 'y';

            Guard G(cache.cacheLock);

            ChannelCache::entries_t::const_iterator it = cache.entries.find(newName);

            if(it==cache.entries.end()) {
                // first request, create ChannelCacheEntry
                //TODO: async lookup
                cache.get(newName);

                assert(cache.entries.size()>0);

            } else if(it->second->channel->isConnected()) {
                // another request, and hey we're connected this time

                ret=this->shared_from_this();
                found=true;
                std::cerr<<"GWServer accepting "<<channelName<<" as "<<newName<<"\n";

                it->second->dropPoke = true;

            } else {
                // not connected yet, but a client is still interested
                it->second->dropPoke = true;
                std::cout<<"cache poke "<<newName<<"\n";
            }
        }

        // unlock for callback

        channelFindRequester->channelFindResult(pvd::Status::Ok, ret, found);

        return ret;
    }

    virtual pva::ChannelFind::shared_pointer channelList(pva::ChannelListRequester::shared_pointer const & channelListRequester)
    {
        std::cerr<<"GWServer does not advertise a channel list\n";
        return pva::ChannelFind::shared_pointer();
    }

    virtual pva::Channel::shared_pointer createChannel(std::string const & channelName,
                                                       pva::ChannelRequester::shared_pointer const & channelRequester,
                                                       short priority = PRIORITY_DEFAULT)
    {
        return createChannel(channelName, channelRequester, priority, "foobar");
    }

    // The return value of this function is ignored
    // The newly created channel is given to the ChannelRequester
    virtual pva::Channel::shared_pointer createChannel(std::string const & channelName,
                                                       pva::ChannelRequester::shared_pointer const & channelRequester,
                                                       short priority, std::string const & address)
    {
        GWChannel::shared_pointer ret;
        std::string newName;

        if(!channelName.empty() && channelName[0]=='x')
        {

            // rewrite name
            newName = channelName;
            newName[0] = 'y';

            Guard G(cache.cacheLock);

            ChannelCache::entries_t::const_iterator it = cache.entries.find(newName);
            if(it!=cache.entries.end() && it->second->channel->isConnected())
            {
                ret.reset(new GWChannel(it->second, channelRequester));
                it->second->interested.insert(ret);
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
        }

        return ret; // ignored by caller
    }

    virtual void configure(epics::pvData::PVStructure::shared_pointer /*configuration*/) {
        std::cout<<"GWServer being configured\n";
    }

    virtual void destroy()
    {
        std::cout<<"GWServer destory request\n";
    }

    GWServerChannelProvider()
    {
        std::cout<<"GW Server ctor\n";
    }
    virtual ~GWServerChannelProvider()
    {
        std::cout<<"GW Server dtor\n";
    }
};

struct GWServerChannelProviderFactory : public pva::ChannelProviderFactory
{
    pva::ChannelProvider::weak_pointer last_provider;

    virtual std::string getFactoryName()
    {
        return "GWServer";
    }

    virtual pva::ChannelProvider::shared_pointer sharedInstance()
    {
        pva::ChannelProvider::shared_pointer P(last_provider.lock());
        if(!P) {
            P.reset(new GWServerChannelProvider);
            ((GWServerChannelProvider*)P.get())->cache.server = P;
            last_provider = P;
        }
        return P;
    }

    virtual pva::ChannelProvider::shared_pointer newInstance()
    {
        pva::ChannelProvider::shared_pointer P(new GWServerChannelProvider);
        ((GWServerChannelProvider*)P.get())->cache.server = P;
        last_provider = P;
        return P;
    }
};

static
bool p2pServerRunning;

static
std::tr1::weak_ptr<pva::ServerContextImpl> gblctx;

static
void runGWServer(void *)
{
    printf("Gateway server starting\n");
    try{
        pva::ServerContextImpl::shared_pointer ctx(pva::ServerContextImpl::create());

        ctx->setChannelProviderName("GWServer");

        ctx->initialize(pva::getChannelProviderRegistry());
        ctx->printInfo();

        printf("Gateway running\n");
        gblctx = ctx;
        ctx->run(0); // zero means forever ?
        gblctx.reset();
        printf("Gateway stopping\n");

        ctx->destroy();
    }catch(std::exception& e){
        printf("Gateway server error: %s\n", e.what());
        gblctx.reset();
    }
    printf("Gateway stopped\n");
    p2pServerRunning = false;
}

void startServer()
{
    if(p2pServerRunning) {
        printf("Already started\n");
        return;
    }

    epicsThreadMustCreate("gwserv",
                          epicsThreadPriorityCAServerLow-2,
                          epicsThreadGetStackSize(epicsThreadStackSmall),
                          &runGWServer, NULL);

    p2pServerRunning = true;
}

void stopServer()
{
    pva::ServerContextImpl::shared_pointer ctx(gblctx.lock());

    if(ctx.get()) {
        printf("Reqesting stop\n");
        ctx->shutdown();
    } else
        printf("Not running\n");
}

void statusServer(int lvl)
{
    try{
        pva::ServerContextImpl::shared_pointer ctx(gblctx);
        if(!ctx) {
            std::cout<<"Not running\n";
            return;
        }

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

            {
                Guard G(scp->cache.cacheLock);

                std::cout<<"Cache has "<<scp->cache.entries.size()<<" channels\n";

                if(lvl>0)
                    entries = scp->cache.entries; // copy of std::map
            }

            if(lvl<=0)
                continue;

            for(ChannelCache::entries_t::const_iterator it=entries.begin(), end=entries.end();
                it!=end; ++it)
            {
                ChannelCacheEntry& E = *it->second;
                std::cout<<pva::Channel::ConnectionStateNames[E.channel->getConnectionState()]
                        <<" Channel '"<<E.channelName<<"' with "<<E.interested.size()<<" clients\n";
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
        pva::ServerContextImpl::shared_pointer ctx(gblctx);
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
                scp->cache.entries.erase(it); // drop out of cache
            }

            entry->channel->destroy(); // trigger client side disconnect
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
        pva::ServerContextImpl::shared_pointer ctx(gblctx);
        if(!ctx) {
            std::cout<<"Not running\n";
            return;
        }

        const AUTO_REF(prov, ctx->getChannelProviders());

        size_t chan_count = 0, mon_count = 0, mon_user_count = 0;

        if(lvl>0) std::cout<<"Server has "<<prov.size()<<" providers\n";

        for(size_t i=0; i<prov.size(); i++)
        {
            pva::ChannelProvider* p = prov[i].get();
            if(!p) continue;
            GWServerChannelProvider *scp = dynamic_cast<GWServerChannelProvider*>(p);
            if(!scp) continue;

            {
                Guard G(scp->cache.cacheLock);
                AUTO_REF(entries, scp->cache.entries);

                if(lvl>0) std::cout<<" Cache has "<<scp->cache.entries.size()<<" channels\n";

                chan_count += entries.size();

                FOREACH(it, end, entries)
                {
                    AUTO_VAL(M, it->second->mon_entries.lock_vector());

                    if(lvl>0) std::cout<<"  Channel "<<it->second->channelName
                                      <<" has "<<M.size()<<" Client Monitors\n";

                    mon_count += M.size();
                    FOREACH(it2, end2, M)
                    {
                        AUTO_REF(W, it2->second->interested);
                        if(lvl>0) std::cout<<"   Used by "<<W.size()<<" Client Monitors\n";
                        mon_user_count += W.size();
                    }
                }
            }
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

} // namespace

static
pva::ChannelProviderFactory::shared_pointer GWServerFactory;

void registerGWServerIocsh()
{
    GWServerFactory.reset(new GWServerChannelProviderFactory);
    pva::registerChannelProviderFactory(GWServerFactory);

    iocshRegister<&startServer>("gwstart");
    iocshRegister<&stopServer>("gwstop");
    iocshRegister<int, &statusServer>("gwstatus", "level");
    iocshRegister<const char*, &dropChannel>("gwdrop", "channel");
    iocshRegister<int, &refCheck>("gwref", "level");

}

void gwServerShutdown()
{
    pva::ServerContextImpl::shared_pointer P(gblctx.lock());
    if(P)
        stopServer();
    if(GWServerFactory)
        unregisterChannelProviderFactory(GWServerFactory);
}
