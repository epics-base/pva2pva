#ifndef PDBGROUP_H
#define PDBGROUP_H

#include <dbAccess.h>

#include <dbEvent.h>
#include <dbLock.h>

#include <pv/pvAccess.h>

#include "pvahelper.h"
#include "pvif.h"
#include "pdb.h"

struct PDBGroupMonitor;

void pdb_group_event(void *user_arg, struct dbChannel *chan,
                     int eventsRemaining, struct db_field_log *pfl);

struct PDBGroupPV : public PDBPV
{
    POINTER_DEFINITIONS(PDBGroupPV);
    weak_pointer weakself;
    inline shared_pointer shared_from_this() { return shared_pointer(weakself); }

    epicsMutex lock;

    // get/put/monitor
    std::string name;
    epics::pvData::shared_vector<DBCH> chan;
    std::vector<std::string> attachments;
    DBManyLock locker;

    // monitor only
    epics::pvData::BitSet scratch;
    std::vector<std::tr1::shared_ptr<PVIF> > pvif;
    epics::pvData::shared_vector<DBEvent> evts_VALUE, evts_PROPERTY;

    epics::pvData::PVStructurePtr complete; // complete copy from subscription

    typedef std::set<std::tr1::shared_ptr<PDBGroupMonitor> > interested_t;
    interested_t interested;
    bool hadevent;

    static size_t ninstances;

    PDBGroupPV();
    virtual ~PDBGroupPV();

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

    PDBGroupChannel(const PDBGroupPV::shared_pointer& pv,
                const std::tr1::shared_ptr<epics::pvAccess::ChannelProvider>& prov,
                const epics::pvAccess::ChannelRequester::shared_pointer& req);
    virtual ~PDBGroupChannel() {}

    virtual epics::pvAccess::ChannelGet::shared_pointer createChannelGet(
            epics::pvAccess::ChannelGetRequester::shared_pointer const & channelGetRequester,
            epics::pvData::PVStructure::shared_pointer const & pvRequest);
    virtual epics::pvAccess::ChannelPut::shared_pointer createChannelPut(
            epics::pvAccess::ChannelPutRequester::shared_pointer const & requester,
            epics::pvData::PVStructure::shared_pointer const & pvRequest);
    virtual epics::pvData::Monitor::shared_pointer createMonitor(
            epics::pvData::MonitorRequester::shared_pointer const & requester,
            epics::pvData::PVStructure::shared_pointer const & pvRequest);

    virtual void printInfo(std::ostream& out);
};

struct PDBGroupGet : public epics::pvAccess::ChannelGet,
        public std::tr1::enable_shared_from_this<PDBGroupGet>
{
    PDBGroupChannel::shared_pointer channel;
    epics::pvAccess::ChannelGetRequester::shared_pointer requester;

    bool atomic;
    epics::pvData::BitSetPtr changed;
    epics::pvData::PVStructurePtr pvf;
    std::vector<std::tr1::shared_ptr<PVIF> > pvif;

    PDBGroupGet(const PDBGroupChannel::shared_pointer& channel,
                const epics::pvAccess::ChannelGetRequester::shared_pointer& requester,
                const epics::pvData::PVStructure::shared_pointer& pvReq);
    virtual ~PDBGroupGet() {}

    virtual void destroy() { pvif.clear(); channel.reset(); requester.reset(); }
    virtual void lock() {}
    virtual void unlock() {}
    virtual std::tr1::shared_ptr<epics::pvAccess::Channel> getChannel() { return channel; }
    virtual void cancel() {}
    virtual void lastRequest() {}
    virtual void get();
};

struct PDBGroupPut : public epics::pvAccess::ChannelPut,
        public std::tr1::enable_shared_from_this<PDBGroupPut>
{
    typedef epics::pvAccess::ChannelPutRequester requester_t;
    PDBGroupChannel::shared_pointer channel;
    requester_t::shared_pointer requester;

    bool atomic;
    epics::pvData::BitSetPtr changed;
    epics::pvData::PVStructurePtr pvf;
    std::vector<std::tr1::shared_ptr<PVIF> > pvif;

    PDBGroupPut(const PDBGroupChannel::shared_pointer &channel,
                const epics::pvAccess::ChannelPutRequester::shared_pointer &requester,
                const epics::pvData::PVStructure::shared_pointer& pvReq);
    virtual ~PDBGroupPut() {}

    virtual void destroy() { pvif.clear(); channel.reset(); requester.reset(); }
    virtual void lock() {}
    virtual void unlock() {}
    virtual std::tr1::shared_ptr<epics::pvAccess::Channel> getChannel() { return channel; }
    virtual void cancel() {}
    virtual void lastRequest() {}
    virtual void put(
            epics::pvData::PVStructure::shared_pointer const & pvPutStructure,
            epics::pvData::BitSet::shared_pointer const & putBitSet);
    virtual void get();
};

struct PDBGroupMonitor : public BaseMonitor
{
    POINTER_DEFINITIONS(PDBGroupMonitor);

    PDBGroupPV::shared_pointer pv;

    bool atomic;

    PDBGroupMonitor(const PDBGroupPV::shared_pointer& pv,
                     const requester_t::shared_pointer& requester,
                     const epics::pvData::PVStructure::shared_pointer& pvReq);
    virtual ~PDBGroupMonitor() {destroy();}

    virtual void onStart();
    virtual void onStop();
    virtual void requestUpdate();

    virtual void destroy();

};

#endif // PDBGROUP_H
