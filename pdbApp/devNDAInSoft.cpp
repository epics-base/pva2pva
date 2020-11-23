/*************************************************************************\
* Copyright (c) 2020 Michael Davidsaver
* EPICS BASE is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/
#ifndef USE_TYPED_DSET
#  define USE_TYPED_DSET
#endif

#include <dbAccess.h>
#include <recGbl.h>

// include by ndainRecord.h
#include "epicsTypes.h"
#include "link.h"
#include "epicsMutex.h"
#include "ellLib.h"
#include "devSup.h"
#include "epicsTime.h"
#include "alarm.h"
#include "errlog.h"

#include <pv/qsrv.h>

#include "ndainRecord.h"
#include <epicsExport.h>

namespace {

namespace pvd = epics::pvData;

long init_record(struct dbCommon *pcommon)
{
    return 0;
}

long readLocked(struct link *pinp, void *raw)
{
    ndainRecord *prec = (ndainRecord *) pinp->precord;

    VSharedVector ival;
    ival.vtype = &vfSharedVector;
    ival.value = &prec->val;

    // prefer reading shared_vector directly
    long status = dbGetLink(pinp, DBR_VFIELD, &ival, 0, 0);

    if (status==S_db_badDbrtype && !dbLinkIsConstant(pinp)) {
        // fallback to reading (and copying) DBF array
        status=0;

        int dbf = dbGetLinkDBFtype(pinp);
        pvd::ScalarType stype;

        switch(dbf) {
#define CASE_SKIP_BOOL
#define CASE_REAL_INT64
#define CASE(BASETYPE, PVATYPE, DBFTYPE, PVACODE) case DBF_##DBFTYPE: stype = pvd::pv##PVACODE; break;
#include <pv/typemap.h>
#undef CASE
#undef CASE_SKIP_BOOL
#undef CASE_REAL_INT64
        default:
            // include DBF_STRING
            recGblSetSevrMsg(prec, READ_ALARM, INVALID_ALARM, "Bad Img DBF %d", dbf);
            return S_db_badDbrtype;
        }

        long nelem = 0;

        if(dbf==-1 || (status=dbGetNelements(pinp, &nelem))) {
            return status;
        }

        size_t esize = dbValueSize(dbf);
        try {
            pvd::shared_vector<char> temp(nelem*esize);

            status = dbGetLink(pinp, dbf, temp.data(), 0, &nelem);

            if(!status) {
                temp.resize(nelem*esize);
                prec->val = pvd::static_shared_vector_cast<const void>(pvd::freeze(temp));
                prec->val.set_original_type(stype);
            }
        } catch(std::exception& e) {
            errlogPrintf("%s: Error during alloc/copy of DBF -> PVD: %s\n", prec->name, e.what());
            recGblSetSevrMsg(prec, READ_ALARM, INVALID_ALARM, "DBF CPY %s", e.what());
            status = S_rec_outMem; /* this is likely an allocation error */
        }
    }

    if (status) return status;

    if (dbLinkIsConstant(&prec->tsel) &&
        prec->tse == epicsTimeEventDeviceTime)
        dbGetTimeStampTag(pinp, &prec->time, &prec->utag);

    return status;
}

long read_nda(ndainRecord* prec)
{
    long status = 0;

    if(!status)
        status = dbLinkDoLocked(&prec->inp, readLocked, 0);

    if (status == S_db_noLSET)
        status = readLocked(&prec->inp, 0);

    if (!status && !dbLinkIsConstant(&prec->inp))
        prec->udf = FALSE;

    return status;
}

ndaindset devNDAISoft = {
    {5, NULL, NULL, &init_record, NULL},
    &read_nda
};

}
extern "C" {
epicsExportAddress(dset, devNDAISoft);
}
