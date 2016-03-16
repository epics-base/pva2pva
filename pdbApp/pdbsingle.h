#ifndef PDBSINGLE_H
#define PDBSINGLE_H

#include <deque>

#include <dbAccess.h>

#include <dbEvent.h>

#include <pv/pvAccess.h>

#include "pvahelper.h"
#include "pvif.h"
#include "pdb.h"

struct PDBSingleMonitor;

struct PDBSinglePV : public PDBPV, public std::tr1::enable_shared_from_this<PDBSinglePV>
{
    POINTER_DEFINITIONS(PDBSinglePV);

    /* this dbChannel is shared by all operations,
     * which is safe as it's modify-able field(s) (addr.pfield)
     * are only access while the underlying record
     * is locked.
     */
    DBCH chan;
    PDBProvider::shared_pointer provider;

    epicsMutex lock;

    epics::pvData::BitSet scratch;
    std::auto_ptr<PVIF> pvif;

    epics::pvData::PVStructurePtr complete; // complete copy from subscription

    typedef std::set<std::tr1::shared_ptr<PDBSingleMonitor> > interested_t;
    interested_t interested;

    struct Event {
        PDBSinglePV *self;
        dbEventSubscription subscript;
        unsigned dbe_mask;
        Event(PDBSinglePV *m, unsigned mask);
        ~Event();
        void enable();
    };
    Event evt_VALUE, evt_PROPERTY;
    bool hadevent;

    static size_t ninstances;

    PDBSinglePV(DBCH& chan,
                const PDBProvider::shared_pointer& prov);
    virtual ~PDBSinglePV();

    void activate();

    epics::pvAccess::Channel::shared_pointer
        connect(const std::tr1::shared_ptr<PDBProvider>& prov,
                const epics::pvAccess::ChannelRequester::shared_pointer& req);
};

struct PDBSingleChannel : public BaseChannel,
        public std::tr1::enable_shared_from_this<PDBSingleChannel>
{
    POINTER_DEFINITIONS(PDBSingleChannel);

    PDBSinglePV::shared_pointer pv;

    PDBSingleChannel(const PDBSinglePV::shared_pointer& pv,
                const epics::pvAccess::ChannelRequester::shared_pointer& req);
    virtual ~PDBSingleChannel() {}

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

struct PDBSingleGet : public epics::pvAccess::ChannelGet,
        public std::tr1::enable_shared_from_this<PDBSingleGet>
{
    typedef epics::pvAccess::ChannelGetRequester requester_t;
    PDBSingleChannel::shared_pointer channel;
    requester_t::shared_pointer requester;

    epics::pvData::BitSetPtr changed;
    epics::pvData::PVStructurePtr pvf;
    std::auto_ptr<PVIF> pvif;

    PDBSingleGet(const PDBSingleChannel::shared_pointer& channel,
                 const epics::pvAccess::ChannelGetRequester::shared_pointer& requester,
                 const epics::pvData::PVStructure::shared_pointer& pvReq);
    virtual ~PDBSingleGet() {}

    virtual void destroy() { pvif.reset(); channel.reset(); requester.reset(); }
    virtual void lock() {}
    virtual void unlock() {}
    virtual std::tr1::shared_ptr<epics::pvAccess::Channel> getChannel() { return channel; }
    virtual void cancel() {}
    virtual void lastRequest() {}
    virtual void get();
};

struct PDBSinglePut : public epics::pvAccess::ChannelPut,
        public std::tr1::enable_shared_from_this<PDBSinglePut>
{
    typedef epics::pvAccess::ChannelPutRequester requester_t;
    PDBSingleChannel::shared_pointer channel;
    requester_t::shared_pointer requester;

    epics::pvData::BitSetPtr changed;
    epics::pvData::PVStructurePtr pvf;
    std::auto_ptr<PVIF> pvif;

    PDBSinglePut(const PDBSingleChannel::shared_pointer& channel,
                 const epics::pvAccess::ChannelPutRequester::shared_pointer& requester,
                 const epics::pvData::PVStructure::shared_pointer& pvReq);
    virtual ~PDBSinglePut() {}

    virtual void destroy() { pvif.reset(); channel.reset(); requester.reset(); }
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

struct PDBSingleMonitor : public BaseMonitor
{
    POINTER_DEFINITIONS(PDBSingleMonitor);

    PDBSinglePV::shared_pointer pv;

    PDBSingleMonitor(const PDBSinglePV::shared_pointer& pv,
                     const requester_t::shared_pointer& requester,
                     const epics::pvData::PVStructure::shared_pointer& pvReq);
    virtual ~PDBSingleMonitor() {destroy();}

    virtual void onStart();
    virtual void onStop();

    virtual void destroy();
};

#endif // PDBSINGLE_H
