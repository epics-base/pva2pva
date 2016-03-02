#include <dbAccess.h>
#include <epicsAtomic.h>

#include "pdbsingle.h"
#include "pdb.h"

namespace pvd = epics::pvData;
namespace pva = epics::pvAccess;

size_t PDBSinglePV::ninstances;

PDBSinglePV::PDBSinglePV(DBCH& chan,
            const PDBProvider::shared_pointer& prov)
    :provider(prov)
{
    this->chan.swap(chan);
    fielddesc = PVIF::dtype(this->chan);
    epics::atomic::increment(ninstances);
}

PDBSinglePV::~PDBSinglePV()
{
    epics::atomic::decrement(ninstances);
}

pva::Channel::shared_pointer
PDBSinglePV::connect(const std::tr1::shared_ptr<PDBProvider>& prov,
                     const pva::ChannelRequester::shared_pointer& req)
{
    pva::Channel::shared_pointer ret(new PDBSingleChannel(shared_from_this(), req));
    return ret;
}

PDBSingleChannel::PDBSingleChannel(const PDBSinglePV::shared_pointer& pv,
                                   const pva::ChannelRequester::shared_pointer& req)
    :BaseChannel(dbChannelName(pv->chan), pv->provider, req, pv->fielddesc)
    ,pv(pv)
{
}

void PDBSingleChannel::printInfo(std::ostream& out)
{
    out<<"PDB single : "<<pvname<<"\n";
}

pva::ChannelGet::shared_pointer
PDBSingleChannel::createChannelGet(
        pva::ChannelGetRequester::shared_pointer const & requester,
        pvd::PVStructure::shared_pointer const & pvRequest)
{
    pva::ChannelGet::shared_pointer ret(new PDBSingleGet(shared_from_this(), requester));
    requester->channelGetConnect(pvd::Status(), ret, fielddesc);
    return ret;
}

pva::ChannelPut::shared_pointer
PDBSingleChannel::createChannelPut(
        pva::ChannelPutRequester::shared_pointer const & requester,
        pvd::PVStructure::shared_pointer const & pvRequest)
{
    pva::ChannelPut::shared_pointer ret(new PDBSinglePut(shared_from_this(), requester));
    requester->channelPutConnect(pvd::Status(), ret, fielddesc);
    return ret;
}


PDBSingleGet::PDBSingleGet(PDBSingleChannel::shared_pointer channel,
                           pva::ChannelGetRequester::shared_pointer requester)
    :channel(channel)
    ,requester(requester)
    ,changed(new pvd::BitSet(channel->fielddesc->getNumberFields()))
    ,pvf(pvd::getPVDataCreate()->createPVStructure(channel->fielddesc))
    ,pvif(PVIF::attach(channel->pv->chan, pvf))
{}

void PDBSingleGet::get()
{
    changed->clear();
    {
        DBScanLocker L(channel->pv->chan);
        pvif->put(*changed, DBE_VALUE|DBE_ALARM|DBE_PROPERTY, NULL);
    }
    //TODO: report unused fields as changed?
    changed->clear();
    changed->set(0);
    requester->getDone(pvd::Status(), shared_from_this(), pvf, changed);
}




PDBSinglePut::PDBSinglePut(PDBSingleChannel::shared_pointer channel,
                           pva::ChannelPutRequester::shared_pointer requester)
    :channel(channel)
    ,requester(requester)
    ,changed(new pvd::BitSet(channel->fielddesc->getNumberFields()))
    ,pvf(pvd::getPVDataCreate()->createPVStructure(channel->fielddesc))
    ,pvif(PVIF::attach(channel->pv->chan, pvf))
{}

void PDBSinglePut::put(pvd::PVStructure::shared_pointer const & value,
                       pvd::BitSet::shared_pointer const & changed)
{
    {
        DBScanLocker L(channel->pv->chan);
        pvif->get(*changed);
    }
    requester->putDone(pvd::Status(), shared_from_this());
}

void PDBSinglePut::get()
{
    changed->clear();
    {
        DBScanLocker L(channel->pv->chan);
        pvif->put(*changed, DBE_VALUE|DBE_ALARM|DBE_PROPERTY, NULL);
    }
    //TODO: report unused fields as changed?
    changed->clear();
    changed->set(0);
    requester->getDone(pvd::Status(), shared_from_this(), pvf, changed);
}
