
#include <vector>
#include <utility>

#include <errlog.h>

#include "helper.h"
#include "pdbsingle.h"
#include "pdbgroup.h"
#include "pvif.h"

namespace pvd = epics::pvData;
namespace pva = epics::pvAccess;

int PDBProviderDebug = 3;

namespace {
struct GroupMemberInfo {
    GroupMemberInfo(const std::string& a, const std::string& b) :pvname(a), pvfldname(b) {}
    std::string pvname, pvfldname;
};

struct GroupInfo {
    GroupInfo(const std::string& name) : name(name) {}
    std::string name;
    typedef std::vector<GroupMemberInfo> members_t;
    members_t members;
};
}

PDBProvider::PDBProvider()
{
    typedef std::map<std::string, GroupInfo> groups_t;
    groups_t groups;

    for(pdbRecordIterator rec; !rec.done(); rec.next())
    {
        const char * const group = rec.info("pdbGroup");
        if(!group)
            continue;
        /* syntax:
         *   info(pdbGroup, "<groupname>|field=FLD|other=FLD2?{}|...")
         */
        try {
            std::string recbase(rec.name());
            recbase+=".";

            const char *pos=strchr(group, '|');
            if(!pos)
                throw std::runtime_error("missing '|' after group name");
            else if(pos==group)
                throw std::runtime_error("empty group name not allowed");

            std::string groupname(group, pos-group);

            groups_t::iterator it = groups.find(groupname);
            if(it==groups.end()) {
                // lazy creation of group
                std::pair<groups_t::iterator, bool> ins(groups.insert(std::make_pair(groupname, GroupInfo(groupname))));
                it = ins.first;
            }
            GroupInfo& info = it->second;
            pos++;

            while(pos && *pos) {
                const char *next = strchr(pos, '|'),
                           *equal= strchr(pos, '=');

                if(!equal || (next && equal>next))
                    throw std::runtime_error("expected '='");

                info.members.push_back(GroupMemberInfo(recbase + (next ? std::string(equal+1, next-equal-1) : std::string(equal+1)),
                                                       std::string(pos, equal-pos)));

                if(PDBProviderDebug>2) {
                    fprintf(stderr, "  pdbGroup '%s' add '%s'='%s'\n",
                            info.name.c_str(),
                            info.members.back().pvfldname.c_str(),
                            info.members.back().pvname.c_str());
                }

                if(next) next++;
                pos = next;
            }

        } catch(std::exception& e) {
            fprintf(stderr, "%s: has invalid pdbGroup spec '%s': %s\n", rec.name(), group, e.what());
        }
    }

    pvd::FieldBuilderPtr builder(pvd::getFieldCreate()->createFieldBuilder());
    pvd::PVDataCreatePtr pvbuilder(pvd::getPVDataCreate());

    FOREACH(it, end, groups)
    {
        GroupInfo &info=it->second;
        try{
            if(persist_pv_map.find(info.name)!=persist_pv_map.end())
                throw std::runtime_error("name already in used");

            const size_t nchans = info.members.size();

            PDBGroupPV::shared_pointer pv(new PDBGroupPV());
            pv->weakself = pv;
            pv->name = info.name;
            pv->attachments.resize(nchans);
            pvd::shared_vector<DBCH> chans(nchans);
            std::vector<dbCommon*> records(nchans);

            for(size_t i=0; i<nchans; i++)
            {
                GroupMemberInfo &mem = info.members[i];

                DBCH chan(mem.pvname);

                builder->add(mem.pvfldname, PVIF::dtype(chan));

                pv->attachments[i] = mem.pvfldname;
                records[i] = dbChannelRecord(chan);
                chans[i].swap(chan);
            }

            pv->fielddesc = builder->createStructure();
            pv->complete = pvbuilder->createPVStructure(pv->fielddesc);
            pv->chan.swap(chans);

            DBManyLock L(&records[0], records.size(), 0);
            pv->locker.swap(L);

            persist_pv_map[info.name] = pv;

        }catch(std::exception& e){
            fprintf(stderr, "%s: pdbGroup not created: %s\n", info.name.c_str(), e.what());
        }
    }

    event_context = db_init_events();
    if(!event_context)
        throw std::runtime_error("Failed to create dbEvent context");
    int ret = db_start_events(event_context, "PDB-event", NULL, NULL, epicsThreadPriorityCAServerLow-1);
    if(ret!=DB_EVENT_OK)
        throw std::runtime_error("Failed to stsart dbEvent context");

    try {
        FOREACH(it, end, persist_pv_map)
        {
            const PDBPV::shared_pointer& ppv = it->second;
            PDBGroupPV *pv = dynamic_cast<PDBGroupPV*>(ppv.get());
            if(!pv)
                continue;
            const size_t nchans = pv->chan.size();

            // prepare for monitor

            pv->pvif.resize(nchans);
            epics::pvData::shared_vector<DBEvent> values(nchans), props(nchans);

            for(size_t i=0; i<nchans; i++)
            {
                pv->pvif[i].reset(PVIF::attach(pv->chan[i],
                                               pv->complete->getSubFieldT<pvd::PVStructure>(pv->attachments[i])));

                values[i].create(event_context, pv->chan[i], &pdb_group_event, DBE_VALUE|DBE_ALARM);
                values[i].self = pv;
                props[i].create(event_context, pv->chan[i], &pdb_group_event, DBE_PROPERTY);
                props[i].self = pv;
                values[i].index = props[i].index = i;
            }

            pv->evts_VALUE.swap(values);
            pv->evts_PROPERTY.swap(props);
        }
    }catch(...){
        db_close_events(event_context);
        throw;
    }
}

PDBProvider::~PDBProvider()
{
    {
        epicsGuard<epicsMutex> G(transient_pv_map.mutex());
        if(event_context) {
            /* Explicit destroy to ensure that the dbEventCtx
             * is free'd from the event thread.
             */
            errlogPrintf("Warning: PDBProvider free'd without destroy().  Possible race condition\nn");
        }
    }
    destroy();
}

void PDBProvider::destroy()
{
    dbEventCtx ctxt = NULL;
    persist_pv_map_t ppv;
    {
        epicsGuard<epicsMutex> G(transient_pv_map.mutex());
        persist_pv_map.swap(ppv);
        std::swap(ctxt, event_context);
    }
    ppv.clear(); // indirectly calls all db_cancel_events()
    if(ctxt) db_close_events(ctxt);
}

std::string PDBProvider::getProviderName() { return "PDBProvider"; }

namespace {
struct ChannelFindRequesterNOOP : public pva::ChannelFind
{
    const pva::ChannelProvider::weak_pointer provider;
    ChannelFindRequesterNOOP(const pva::ChannelProvider::shared_pointer& prov) : provider(prov) {}
    virtual ~ChannelFindRequesterNOOP() {}
    virtual void destroy() {}
    virtual std::tr1::shared_ptr<pva::ChannelProvider> getChannelProvider() { return provider.lock(); }
    virtual void cancel() {}
};
}

pva::ChannelFind::shared_pointer
PDBProvider::channelFind(const std::string &channelName, const pva::ChannelFindRequester::shared_pointer &requester)
{
    pva::ChannelFind::shared_pointer ret(new ChannelFindRequesterNOOP(shared_from_this()));

    bool found = false;
    {
        epicsGuard<epicsMutex> G(transient_pv_map.mutex());
        if(persist_pv_map.find(channelName)!=persist_pv_map.end()
                || transient_pv_map.find(channelName)
                || dbChannelTest(channelName.c_str())==0)
            found = true;
    }
    requester->channelFindResult(pvd::Status(), ret, found);
    return ret;
}

pva::ChannelFind::shared_pointer
PDBProvider::channelList(pva::ChannelListRequester::shared_pointer const & requester)
{
    pva::ChannelFind::shared_pointer ret;
    requester->channelListResult(pvd::Status(pvd::Status::STATUSTYPE_ERROR, "not supported"),
                                 ret, pvd::PVStringArray::const_svector(), true);
    return ret;
}

pva::Channel::shared_pointer
PDBProvider::createChannel(std::string const & channelName,
                           pva::ChannelRequester::shared_pointer const & channelRequester,
                           short priority)
{
    return createChannel(channelName, channelRequester, priority, "???");
}

pva::Channel::shared_pointer
PDBProvider::createChannel(std::string const & channelName,
                                                               pva::ChannelRequester::shared_pointer const & requester,
                                                               short priority, std::string const & address)
{
    pva::Channel::shared_pointer ret;
    PDBPV::shared_pointer pv;
    pvd::Status status;
    {
        epicsGuard<epicsMutex> G(transient_pv_map.mutex());

        pv = transient_pv_map.find(channelName);
        if(!pv) {
            persist_pv_map_t::const_iterator it=persist_pv_map.find(channelName);
            if(it!=persist_pv_map.end()) {
                pv = it->second;
            }
        }
        if(!pv) {
            dbChannel *pchan = dbChannelCreate(channelName.c_str());
            if(pchan) {
                DBCH chan(pchan);
                pv.reset(new PDBSinglePV(chan, shared_from_this()));
                transient_pv_map.insert(channelName, pv);
                PDBSinglePV::shared_pointer spv = std::tr1::static_pointer_cast<PDBSinglePV>(pv);
                spv->weakself = spv;
                spv->activate();
            }
        }
    }
    if(pv) {
        ret = pv->connect(shared_from_this(), requester);
    }
    if(!ret) {
        status = pvd::Status(pvd::Status::STATUSTYPE_ERROR, "not found");
    }
    requester->channelCreated(status, ret);
    return ret;
}
