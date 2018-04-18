
#include <epicsString.h>

#define epicsExportSharedSymbols
#include <shareLib.h>

#include "pvalink.h"


namespace {

using namespace pvalink;

#define TRY pvaLink *self = static_cast<pvaLink*>(plink->value.json.jlink); assert(self->alive); try
#define CATCH(LOC) catch(std::exception& e) { \
    errlogPrintf("pvaLink " #LOC " fails %s: %s\n", plink->precord->name, e.what()); \
}

void pvaOpenLink(DBLINK *plink)
{
    try {
        pvaLink* self((pvaLink*)plink->value.json.jlink);

        TRACE(<<plink->precord->name<<" "<<self->channelName);

        // still single threaded at this point.
        // also, no pvaLinkChannel::lock yet

        self->plink = plink;

        if(self->channelName.empty())
            return; // nothing to do...

        pvd::PVStructure::const_shared_pointer pvRequest(self->makeRequest());
        pvaGlobal_t::channels_key_t key;

        {
            std::ostringstream strm;
            strm<<*pvRequest; // print the request as a convient key for our channel cache

            key = std::make_pair(self->channelName, strm.str());
        }

        std::tr1::shared_ptr<pvaLinkChannel> chan;
        bool doOpen = false;
        {
            Guard G(pvaGlobal->lock);

            pvaGlobal_t::channels_t::iterator it(pvaGlobal->channels.find(key));

            if(it!=pvaGlobal->channels.end()) {
                // re-use existing channel
                chan = it->second.lock();
            }

            if(!chan) {
                // open new channel

                chan.reset(new pvaLinkChannel(key, pvRequest));
                pvaGlobal->channels.insert(std::make_pair(key, chan));
                doOpen = true;
            }
        }

        if(doOpen) {
            chan->open(); // start subscription
        }

        {
            Guard G(chan->lock);

            chan->links.insert(self);
            chan->links_changed = true;

            self->lchan.swap(chan); // we are now attached
        }

        return;
    }CATCH(pvaOpenLink)
    // on error, prevent any further calls to our lset functions
    plink->lset = NULL;
}

void pvaRemoveLink(struct dbLocker *locker, DBLINK *plink)
{
    try {
        p2p::auto_ptr<pvaLink> self((pvaLink*)plink->value.json.jlink);
        TRACE(<<plink->precord->name<<" "<<self->channelName);
        assert(self->alive);
        Guard G(self->lchan->lock);

    }CATCH(pvaRemoteLink)
}

int pvaIsConnected(const DBLINK *plink)
{
    TRY {
        TRACE(<<plink->precord->name<<" "<<self->channelName);
        Guard G(self->lchan->lock);
        if(!self->valid()) return -1;

        return self->valid();

    }CATCH(pvaIsConnected)
    return 0;
}

int pvaGetDBFtype(const DBLINK *plink)
{
    TRY {
        TRACE(<<plink->precord->name<<" "<<self->channelName);
        Guard G(self->lchan->lock);
        if(!self->valid()) return -1;

        // if fieldName is empty, use top struct value
        // if fieldName not empty
        //    if sub-field is struct, use sub-struct .value
        //    if sub-field not struct, treat as value

        pvd::PVField::const_shared_pointer value(self->getSubField("value"));

        pvd::ScalarType ftype = pvd::pvInt; // default for un-mapable types.
        if(!value) {
            // no-op
        } else if(value->getField()->getType()==pvd::scalar)
            ftype = static_cast<const pvd::Scalar*>(value->getField().get())->getScalarType();
        else if(value->getField()->getType()==pvd::scalarArray)
            ftype = static_cast<const pvd::ScalarArray*>(value->getField().get())->getElementType();

        switch(ftype) {
#define CASE(BASETYPE, PVATYPE, DBFTYPE, PVACODE) case pvd::pv##PVACODE: return DBF_##DBFTYPE;
#define CASE_REAL_INT64
#include "pv/typemap.h"
#undef CASE_REAL_INT64
#undef CASE
        case pvd::pvString: return DBF_STRING; // TODO: long string?
        }

    }CATCH(pvaIsConnected)
    return -1;
}

long pvaGetElements(const DBLINK *plink, long *nelements)
{
    TRY {
        TRACE(<<plink->precord->name<<" "<<self->channelName);
        Guard G(self->lchan->lock);
        if(!self->valid()) return -1;

        pvd::PVField::const_shared_pointer value(self->getSubField("value"));

        if(value && value->getField()->getType()==pvd::scalarArray)
            return static_cast<const pvd::PVScalarArray*>(value.get())->getLength();
        else
            return 0;
    }CATCH(pvaIsConnected)
    return -1;
}

long pvaGetValue(DBLINK *plink, short dbrType, void *pbuffer,
        long *pnRequest)
{
    TRY {
        TRACE(<<plink->precord->name<<" "<<self->channelName);
        Guard G(self->lchan->lock);

        pvd::PVField::const_shared_pointer value(self->getSubField("value"));

        // copy from 'value' into 'pbuffer'
        long status = copyPVD2DBF(value, pbuffer, dbrType, pnRequest);
        if(status) return status;

        if(self->ms != pvaLink::NMS) {
            // TODO
        }

        return 0;
    }CATCH(pvaIsConnected)
    return -1;
}

long pvaGetControlLimits(const DBLINK *plink, double *lo, double *hi)
{
    TRY {
        TRACE(<<plink->precord->name<<" "<<self->channelName);
        Guard G(self->lchan->lock);
        if(!self->valid()) return -1;

        if(self->lchan->connected_latched) {
            pvd::PVScalar::const_shared_pointer value;
            if(lo) {
                value = std::tr1::static_pointer_cast<const pvd::PVScalar>(self->getSubField("control.limitLow"));
                *lo = value ? value->getAs<double>() : 0.0;
            }
            if(hi) {
                value = std::tr1::static_pointer_cast<const pvd::PVScalar>(self->getSubField("control.limitHigh"));
                *hi = value ? value->getAs<double>() : 0.0;
            }
        } else {
            *lo = *hi = 0.0;
        }
        return 0;
    }CATCH(pvaIsConnected)
    return -1;
}

long pvaGetGraphicLimits(const DBLINK *plink, double *lo, double *hi)
{
    TRY {
        TRACE(<<plink->precord->name<<" "<<self->channelName);
        if(!self->valid()) return -1;
        //Guard G(self->lchan->lock);
        *lo = *hi = 0.0;
        return 0;
    }CATCH(pvaIsConnected)
    return -1;
}

long pvaGetAlarmLimits(const DBLINK *plink, double *lolo, double *lo,
        double *hi, double *hihi)
{
    TRY {
        TRACE(<<plink->precord->name<<" "<<self->channelName);
        if(!self->valid()) return -1;
        //Guard G(self->lchan->lock);
        *lolo = *lo = *hi = *hihi = 0.0;
        return 0;
    }CATCH(pvaIsConnected)
    return -1;
}

long pvaGetPrecision(const DBLINK *plink, short *precision)
{
    TRY {
        TRACE(<<plink->precord->name<<" "<<self->channelName);
        if(!self->valid()) return -1;
        //Guard G(self->lchan->lock);
        *precision = 0;
        return 0;
    }CATCH(pvaIsConnected)
    return -1;
}

long pvaGetUnits(const DBLINK *plink, char *units, int unitsSize)
{
    TRY {
        TRACE(<<plink->precord->name<<" "<<self->channelName);
        if(!self->valid()) return -1;
        //Guard G(self->lchan->lock);
        return 0;
    }CATCH(pvaIsConnected)
    return -1;
}

long pvaGetAlarm(const DBLINK *plink, epicsEnum16 *status,
        epicsEnum16 *severity)
{
    TRY {
        TRACE(<<plink->precord->name<<" "<<self->channelName);
        if(!self->valid()) return -1;
        Guard G(self->lchan->lock);
        epicsEnum16 stat = NO_ALARM;

        pvd::PVScalar::const_shared_pointer afld;

        if(severity && (afld = std::tr1::static_pointer_cast<const pvd::PVScalar>(self->getSubField("alarm.severity")))) {
            *severity = afld->getAs<pvd::uint16>();
            // no direct translation for NT alarm status codes
            stat = LINK_ALARM;
        }
        if(status) {
            *status = stat;
        }

        return 0;
    }CATCH(pvaIsConnected)
    return -1;
}

long pvaGetTimeStamp(const DBLINK *plink, epicsTimeStamp *pstamp)
{
    TRY {
        TRACE(<<plink->precord->name<<" "<<self->channelName);
        if(!self->valid()) return -1;
        Guard G(self->lchan->lock);
        pvd::PVScalar::const_shared_pointer afld;

        if(afld = std::tr1::static_pointer_cast<const pvd::PVScalar>(self->getSubField("timeStamp.secondsPastEpoch"))) {
            pstamp->secPastEpoch = afld->getAs<pvd::uint32>()-POSIX_TIME_AT_EPICS_EPOCH;
        } else {
            return S_time_noProvider;
        }

        if(afld = std::tr1::static_pointer_cast<const pvd::PVScalar>(self->getSubField("timeStamp.nanoseconds"))) {
            pstamp->nsec = afld->getAs<pvd::uint32>();
        } else {
            pstamp->nsec = 0u;
        }

        return 0;
    }CATCH(pvaIsConnected)
    return -1;
}

// note that we handle DBF_ENUM differently than in pvif.cpp
pvd::ScalarType DBR2PVD(short dbr)
{
    switch(dbr) {
#define CASE(BASETYPE, PVATYPE, DBFTYPE, PVACODE) case DBR_##DBFTYPE: return pvd::pv##PVACODE;
#define CASE_SKIP_BOOL
#include "pv/typemap.h"
#undef CASE_SKIP_BOOL
#undef CASE
    case DBF_ENUM: return pvd::pvUShort;
    case DBF_STRING: return pvd::pvString;
    }
    throw std::invalid_argument("Unsupported DBR code");
}

long pvaPutValue(DBLINK *plink, short dbrType,
        const void *pbuffer, long nRequest)
{
    TRY {
        TRACE(<<plink->precord->name<<" "<<self->channelName);
        (void)self;
        Guard G(self->lchan->lock);

        if(nRequest < 0) return -1;

        if(!self->valid()) {
            // TODO: option to queue while disconnected
            return -1;
        }

        pvd::ScalarType stype = DBR2PVD(dbrType);

        pvd::shared_vector<const void> buf;

        if(dbrType == DBF_STRING) {
            const char *sbuffer = (const char*)pbuffer;
            pvd::shared_vector<std::string> sval(nRequest);

            for(long n=0; n<nRequest; n++, sbuffer += MAX_STRING_SIZE) {
                sval[n] = std::string(sbuffer, epicsStrnLen(sbuffer, MAX_STRING_SIZE));
            }

            self->put_scratch = pvd::static_shared_vector_cast<const void>(pvd::freeze(sval));

        } else {
            pvd::shared_vector<void> val(pvd::ScalarTypeFunc::allocArray(stype, size_t(nRequest)));

            assert(size_t(dbValueSize(dbrType)*nRequest) == val.size());

            memcpy(val.data(), pbuffer, val.size());

            self->put_scratch = pvd::freeze(val);
        }

        self->used_scratch = true;

        if(!self->defer) self->lchan->put();

        return 0;
    }CATCH(pvaIsConnected)
    return -1;
}

void pvaScanForward(DBLINK *plink)
{
    TRY {
        TRACE(<<plink->precord->name<<" "<<self->channelName);
        Guard G(self->lchan->lock);
    }CATCH(pvaIsConnected)
}

#undef TRY
#undef CATCH

} //namespace

namespace pvalink {

lset pva_lset = {
    0, 1, // non-const, volatile
    &pvaOpenLink,
    &pvaRemoveLink,
    NULL, NULL, NULL,
    &pvaIsConnected,
    &pvaGetDBFtype,
    &pvaGetElements,
    &pvaGetValue,
    &pvaGetControlLimits,
    &pvaGetGraphicLimits,
    &pvaGetAlarmLimits,
    &pvaGetPrecision,
    &pvaGetUnits,
    &pvaGetAlarm,
    &pvaGetTimeStamp,
    &pvaPutValue,
    NULL,
    &pvaScanForward
    //&pvaReportLink,
};

} //namespace pvalink
