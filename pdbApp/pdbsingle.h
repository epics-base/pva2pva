#ifndef PDBSINGLE_H
#define PDBSINGLE_H

#include <dbAccess.h>

#include <dbEvent.h>

#include <pv/pvAccess.h>

#include "pvahelper.h"
#include "pvif.h"
#include "pdb.h"

struct PDBSinglePV : public PDBPV, public std::tr1::enable_shared_from_this<PDBSinglePV>
{
    POINTER_DEFINITIONS(PDBSinglePV);

    DBCH chan;
    PDBProvider::shared_pointer provider;

    PDBSinglePV(DBCH& chan,
                const PDBProvider::shared_pointer& prov)
        :provider(prov)
    {
        this->chan.swap(chan);
    }
    virtual ~PDBSinglePV() {}

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

    virtual void printInfo(std::ostream& out);
};

struct PDBSingleGet : public epics::pvAccess::ChannelGet,
        public std::tr1::enable_shared_from_this<PDBSingleGet>
{
    PDBSingleChannel::shared_pointer channel;
    epics::pvAccess::ChannelGetRequester::shared_pointer requester;

    epics::pvData::BitSetPtr changed;
    epics::pvData::PVStructurePtr pvf;
    std::auto_ptr<PVIF> pvif;

    PDBSingleGet(PDBSingleChannel::shared_pointer channel,
                 epics::pvAccess::ChannelGetRequester::shared_pointer requester);
    virtual ~PDBSingleGet() {}

    virtual void destroy() { pvif.reset(); channel.reset(); requester.reset(); }
    virtual void lock() {}
    virtual void unlock() {}
    virtual std::tr1::shared_ptr<epics::pvAccess::Channel> getChannel() { return channel; }
    virtual void cancel() {}
    virtual void lastRequest() {}
    virtual void get();
};
//struct PDBSinglePut : public epics::pvAccess::ChannelPut {};
//struct PDBSingleMonitor : public epics::pvData::Monitor {};

#endif // PDBSINGLE_H
