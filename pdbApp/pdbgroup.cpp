#include <dbAccess.h>

#include "pdbgroup.h"
#include "pdb.h"

PDBGroupChannel::PDBGroupChannel(const PDBGroupPV::shared_pointer& pv,
                                 const std::tr1::shared_ptr<epics::pvAccess::ChannelProvider>& prov,
                                 const epics::pvAccess::ChannelRequester::shared_pointer& req)
    :BaseChannel(dbChannelName(chan), prov, req, PVIF::dtype(dbChannelFinalFieldType(chan)))
{
    this->chan.swap(chan); // we take ownership
}

epics::pvAccess::ChannelGet::shared_pointer
PDBGroupChannel::createChannelGet(
        epics::pvAccess::ChannelGetRequester::shared_pointer const & requester,
        epics::pvData::PVStructure::shared_pointer const & pvRequest)
{
    epics::pvAccess::ChannelGet::shared_pointer ret(new PDBGroupGet(shared_from_this()));
    requester->channelGetConnect(epics::pvData::Status(), ret, fielddesc);
    return ret;
}


PDBGroupGet::PDBGroupGet(PDBGroupChannel::shared_pointer channel,
                           epics::pvAccess::ChannelGetRequester::shared_pointer requester)
    :channel(channel)
    ,requester(requester)
    ,changed(new epics::pvData::BitSet(channel->fielddesc->getNumberFields()))
    ,pvf(epics::pvData::getPVDataCreate()->createPVStructure(channel->fielddesc))
    ,pvif(PVIF::attach(channel->chan, pvif))
{}

void PDBGroupGet::get()
{
    changed->clear();
    {
        DBScanLocker L(channel->chan);
        pvif->put(*changed, DBE_VALUE|DBE_ALARM|DBE_PROPERTY, NULL);
    }
    requester->getDone(epics::pvData::Status(), shared_from_this(), pvf, changed);
}

