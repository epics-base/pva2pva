#include <dbAccess.h>

#include "pdbsingle.h"
#include "pdb.h"

PDBSingleChannel::PDBSingleChannel(const PDBSinglePV::shared_pointer& pv,
                                   const std::tr1::shared_ptr<epics::pvAccess::ChannelProvider>& prov,
                                   const epics::pvAccess::ChannelRequester::shared_pointer& req)
    :BaseChannel(dbChannelName(pv->chan), prov, req, pv->fielddesc)
    ,pv(pv)
{
}

epics::pvAccess::ChannelGet::shared_pointer
PDBSingleChannel::createChannelGet(
        epics::pvAccess::ChannelGetRequester::shared_pointer const & requester,
        epics::pvData::PVStructure::shared_pointer const & pvRequest)
{
    epics::pvAccess::ChannelGet::shared_pointer ret(new PDBSingleGet(shared_from_this()));
    requester->channelGetConnect(epics::pvData::Status(), ret, fielddesc);
    return ret;
}


PDBSingleGet::PDBSingleGet(PDBSingleChannel::shared_pointer channel,
                           epics::pvAccess::ChannelGetRequester::shared_pointer requester)
    :channel(channel)
    ,requester(requester)
    ,changed(new epics::pvData::BitSet(channel->fielddesc->getNumberFields()))
    ,pvf(epics::pvData::getPVDataCreate()->createPVStructure(channel->fielddesc))
    ,pvif(PVIF::attach(channel->chan, pvif))
{}

void PDBSingleGet::get()
{
    changed->clear();
    {
        DBScanLocker L(channel->chan);
        pvif->put(*changed, DBE_VALUE|DBE_ALARM|DBE_PROPERTY, NULL);
    }
    requester->getDone(epics::pvData::Status(), shared_from_this(), pvf, changed);
}
