#ifndef PDBGROUP_H
#define PDBGROUP_H

#include <dbAccess.h>

#include <dbEvent.h>
#include <dbLock.h>

#include <pv/pvAccess.h>

#include "pvahelper.h"
#include "pvif.h"
#include "pdb.h"

struct PDBGroupPV : public PDBPV
{
    POINTER_DEFINITIONS(PDBGroupPV);

    epics::pvData::shared_vector<DBCH> chan;
    std::auto_ptr<dbLocker> locker;

    PDBGroupPV(const std::vector<std::string>& names);

    virtual
    epics::pvAccess::Channel::shared_pointer
        connect(const std::tr1::shared_ptr<PDBProvider>& prov,
                const epics::pvAccess::ChannelRequester::shared_pointer& req);
};

struct PDBGroupChannel : public BaseChannel,
        public std::tr1::enable_shared_from_this<PDBGroupChannel>
{
    POINTER_DEFINITIONS(PDBGroupChannel);

    PDBGroupPV::shared_pointer pv;
    dbChannel *chan;

    PDBGroupChannel(const PDBGroupPV::shared_pointer& pv,
                const std::tr1::shared_ptr<epics::pvAccess::ChannelProvider>& prov,
                const epics::pvAccess::ChannelRequester::shared_pointer& req);
    virtual PDBGroupChannel() {}

    virtual epics::pvAccess::ChannelGet::shared_pointer createChannelGet(
            epics::pvAccess::ChannelGetRequester::shared_pointer const & channelGetRequester,
            epics::pvData::PVStructure::shared_pointer const & pvRequest);

    virtual void printInfo(std::ostream& out);
};

struct PDBGroupGet : public epics::pvAccess::ChannelGet,
        public std::tr1::enable_shared_from_this<PDBGroupGet>
{
    PDBGroupChannel::shared_pointer channel;
    epics::pvAccess::ChannelGetRequester::shared_pointer requester;

    epics::pvData::BitSetPtr changed;
    epics::pvData::PVStructurePtr pvf;
    std::auto_ptr<PVIF> pvif;

    PDBGroupGet(PDBGroupChannel::shared_pointer channel,
                 epics::pvAccess::ChannelGetRequester::shared_pointer requester);
    virtual ~PDBGroupGet() {}

    virtual void destroy() { pvif.reset(); channel.reset(); requester.reset(); }
    virtual void lock() {}
    virtual void unlock() {}
    virtual std::tr1::shared_ptr<Channel> getChannel() { return channel; }
    virtual void cancel() {}
    virtual void lastRequest() {}
    virtual void get();
};
//struct PDBGroupPut : public epics::pvAccess::ChannelPut {};
//struct PDBGroupMonitor : public epics::pvData::Monitor {};

#endif // PDBGROUP_H
