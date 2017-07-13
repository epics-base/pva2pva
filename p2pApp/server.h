#ifndef SERVER_H
#define SERVER_H

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

    explicit GWServerChannelProvider(const std::tr1::shared_ptr<epics::pvAccess::Configuration>& conf);
    explicit GWServerChannelProvider(const epics::pvAccess::ChannelProvider::shared_pointer& prov);
    virtual ~GWServerChannelProvider();
};

#endif // SERVER_H
