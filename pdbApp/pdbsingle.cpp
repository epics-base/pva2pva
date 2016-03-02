#include <dbAccess.h>

#include "pdbsingle.h"
#include "pdb.h"

namespace pvd = epics::pvData;
namespace pva = epics::pvAccess;

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
    requester->getDone(pvd::Status(), shared_from_this(), pvf, changed);
}
