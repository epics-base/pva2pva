#include <dbAccess.h>

#include "pdbgroup.h"
#include "pdb.h"

namespace pvd = epics::pvData;
namespace pva = epics::pvAccess;

PDBGroupChannel::PDBGroupChannel(const PDBGroupPV::shared_pointer& pv,
                                 const std::tr1::shared_ptr<pva::ChannelProvider>& prov,
                                 const pva::ChannelRequester::shared_pointer& req)
    :BaseChannel(pv->name, prov, req, pv->fielddesc)
{
}

void PDBGroupChannel::printInfo(std::ostream& out)
{
    out<<"PDB group : "<<pvname<<"\n";
}

pva::ChannelGet::shared_pointer
PDBGroupChannel::createChannelGet(
        pva::ChannelGetRequester::shared_pointer const & requester,
        pvd::PVStructure::shared_pointer const & pvRequest)
{
    pva::ChannelGet::shared_pointer ret(new PDBGroupGet(shared_from_this(), requester, pvRequest));
    requester->channelGetConnect(pvd::Status(), ret, fielddesc);
    return ret;
}


PDBGroupGet::PDBGroupGet(const PDBGroupChannel::shared_pointer &channel,
                         const pva::ChannelGetRequester::shared_pointer &requester,
                         const pvd::PVStructure::shared_pointer &pvReq)
    :channel(channel)
    ,requester(requester)
    ,atomic(false)
    ,changed(new pvd::BitSet(channel->fielddesc->getNumberFields()))
    ,pvf(pvd::getPVDataCreate()->createPVStructure(channel->fielddesc))
{
    pvd::PVScalarPtr atomicopt(pvReq->getSubField<pvd::PVScalar>("record._options.atomic"));
    if(atomicopt) {
        try {
            atomic = atomicopt->getAs<pvd::boolean>();
        }catch(std::exception& e){
            requester->message("Unable to parse 'atomic' request option.  Default is false.", pvd::warningMessage);
        }
    }

    const size_t npvs = channel->pv->attachments.size();
    pvif.resize(npvs);
    for(size_t i=0; i<npvs; i++)
    {
        pvif[i].reset(PVIF::attach(channel->pv->chan[i],
                               pvf->getSubFieldT<pvd::PVStructure>(channel->pv->attachments[i])
                               ));
    }
}

void PDBGroupGet::get()
{
    const size_t npvs = pvif.size();

    changed->clear();
    if(atomic) {
        DBManyLocker L(channel->pv->locker.get());
        for(size_t i=0; i<npvs; i++)
            pvif[i]->put(*changed, DBE_VALUE|DBE_ALARM|DBE_PROPERTY, NULL);
    } else {

        for(size_t i=0; i<npvs; i++)
        {
            DBScanLocker L(dbChannelRecord(channel->pv->chan[i]));
            pvif[i]->put(*changed, DBE_VALUE|DBE_ALARM|DBE_PROPERTY, NULL);
        }
    }
    requester->getDone(pvd::Status(), shared_from_this(), pvf, changed);
}

