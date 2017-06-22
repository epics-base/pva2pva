#ifndef PDB_H
#define PDB_H

#include <dbEvent.h>

#include <pv/pvAccess.h>

#include "weakmap.h"

struct PDBProvider;

struct PDBPV
{
    POINTER_DEFINITIONS(PDBPV);

    epics::pvData::StructureConstPtr fielddesc;

    PDBPV() {}
    virtual ~PDBPV() {}

    virtual
    epics::pvAccess::Channel::shared_pointer
        connect(const std::tr1::shared_ptr<PDBProvider>& prov,
                const epics::pvAccess::ChannelRequester::shared_pointer& req) =0;
};

struct PDBProvider : public epics::pvAccess::ChannelProvider,
                     public std::tr1::enable_shared_from_this<PDBProvider>
{
    POINTER_DEFINITIONS(PDBProvider);

    PDBProvider();
    virtual ~PDBProvider();
    virtual void destroy();
    virtual std::string getProviderName();
    virtual epics::pvAccess::ChannelFind::shared_pointer channelFind(std::string const & channelName,
                                             epics::pvAccess::ChannelFindRequester::shared_pointer const & channelFindRequester);
    virtual epics::pvAccess::ChannelFind::shared_pointer channelList(epics::pvAccess::ChannelListRequester::shared_pointer const & channelListRequester);
    virtual epics::pvAccess::Channel::shared_pointer createChannel(std::string const & channelName,
                                                                   epics::pvAccess::ChannelRequester::shared_pointer const & channelRequester,
                                           short priority = PRIORITY_DEFAULT);
    virtual epics::pvAccess::Channel::shared_pointer createChannel(std::string const & channelName,
                                                                   epics::pvAccess::ChannelRequester::shared_pointer const & channelRequester,
                                                                   short priority, std::string const & address);

    typedef std::map<std::string, PDBPV::shared_pointer> persist_pv_map_t;
    persist_pv_map_t persist_pv_map;

    typedef weak_value_map<std::string, PDBPV> transient_pv_map_t;
    transient_pv_map_t transient_pv_map;

    dbEventCtx event_context;

    static size_t ninstances;
};

#endif // PDB_H
