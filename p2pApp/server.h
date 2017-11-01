#ifndef SERVER_H
#define SERVER_H

#include <pv/serverContext.h>

#include "chancache.h"
#include "channel.h"

#include <shareLib.h>

struct epicsShareClass GWServerChannelProvider : public
        epics::pvAccess::ChannelProvider,
        epics::pvAccess::ChannelFind,
        std::tr1::enable_shared_from_this<GWServerChannelProvider>
{
    POINTER_DEFINITIONS(GWServerChannelProvider);
    ChannelCache cache;

    virtual std::tr1::shared_ptr<ChannelProvider> getChannelProvider();

    virtual void cancel() {}

    virtual std::string getProviderName() {
        return "GWServer";
    }

    virtual epics::pvAccess::ChannelFind::shared_pointer channelFind(std::string const & channelName,
                                             epics::pvAccess::ChannelFindRequester::shared_pointer const & channelFindRequester);

    using epics::pvAccess::ChannelProvider::createChannel;
    virtual epics::pvAccess::Channel::shared_pointer createChannel(std::string const & channelName,
                                                       epics::pvAccess::ChannelRequester::shared_pointer const & channelRequester,
                                                       short priority, std::string const & addressx);
    virtual void destroy();

    explicit GWServerChannelProvider(const epics::pvAccess::ChannelProvider::shared_pointer& prov);
    virtual ~GWServerChannelProvider();
};

struct epicsShareClass ServerConfig {
    int debug;
    bool interactive;
    epics::pvData::PVStructure::shared_pointer conf;

    typedef std::map<std::string, GWServerChannelProvider::shared_pointer> clients_t;
    clients_t clients;

    typedef std::map<std::string, epics::pvAccess::ServerContext::shared_pointer> servers_t;
    servers_t servers;

    ServerConfig() :debug(0), interactive(true) {}

    void drop(const char *client, const char *channel);
    void status_server(int lvl, const char *server);
    void status_client(int lvl, const char *client, const char *channel);
};

#endif // SERVER_H
