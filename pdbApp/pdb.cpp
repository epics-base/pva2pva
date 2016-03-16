
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

    FOREACH(it, end, groups)
    {
        GroupInfo &info=it->second;
        try{
            if(persist_pv_map.find(info.name)!=persist_pv_map.end())
                throw std::runtime_error("name already in used");

            const size_t nchans = info.members.size();

            PDBGroupPV::shared_pointer pv(new PDBGroupPV());
            pv->name = info.name;
            pv->attachments.resize(nchans);
            //pv->chan.resize(nchans);
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
    {
        epicsGuard<epicsMutex> G(transient_pv_map.mutex());
        std::swap(ctxt, event_context);
    }
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
