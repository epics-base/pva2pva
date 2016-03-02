
#include <vector>
#include <utility>

#include <errlog.h>

#include "helper.h"
#include "pdbsingle.h"
#include "pdbgroup.h"
#include "pvif.h"

namespace pvd = epics::pvData;
namespace pva = epics::pvAccess;

int PDBProviderDebug = 0;

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

                if(!equal || equal>next)
                    throw std::runtime_error("expected '='");

                info.members.push_back(GroupMemberInfo(recbase + (next ? std::string(equal+1, next-pos) : std::string(equal+1)),
                                                       std::string(pos, equal-pos)));

                if(PDBProviderDebug>2) {
                    fprintf(stderr, "  pdbGroup '%s' add %s=%s\n",
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
                pv->attachments[i] = mem.pvfldname;
                records[i] = dbChannelRecord(chan);
                chans[i].swap(chan);
            }
            pv->chan.swap(chans);

            pv->locker.reset(dbLockerAlloc(&records[0], records.size(), 0));
            if(!pv->locker.get())
                throw std::runtime_error("Failed to create dbLocker");

            persist_pv_map[info.name] = pv;

        }catch(std::exception& e){
            fprintf(stderr, "%s: pdbGroup not created: %s\n", info.name.c_str(), e.what());
        }
    }
}

PDBProvider::~PDBProvider() {}

void PDBProvider::destroy() {}

std::string PDBProvider::getProviderName() { return "PDBProvider"; }

pva::ChannelFind::shared_pointer
PDBProvider::channelFind(const std::string &channelName, const pva::ChannelFindRequester::shared_pointer &requester)
{
    pva::ChannelFind::shared_pointer ret;
    requester->channelFindResult(pvd::Status(pvd::Status::STATUSTYPE_ERROR, ""), ret, false);
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
