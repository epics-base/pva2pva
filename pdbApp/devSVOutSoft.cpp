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

// include by svectoroutRecord.h
#include "epicsTypes.h"
#include "link.h"
#include "epicsMutex.h"
#include "ellLib.h"
#include "devSup.h"
#include "epicsTime.h"

#include "svectoroutRecord.h"
#include <epicsExport.h>

namespace {

namespace pvd = epics::pvData;

long writeLocked(struct link *pinp, void *raw)
{
    svectoroutRecord *prec = (svectoroutRecord *) pinp->precord;

    VSharedVector ival;
    ival.vtype = &vfSharedVector;
    ival.value = &prec->val;

    long status = dbPutLink(pinp, DBR_VFIELD, &ival, 1);

    if (status==S_db_badDbrtype && prec->ftvl!=DBF_STRING) {
        status=0;

        size_t esize = dbValueSize(prec->ftvl);
        long nReq = prec->val.size() / esize;

        status = dbPutLink(pinp, prec->ftvl, prec->val.data(), nReq);
    }
    if (status) return status;

    if (dbLinkIsConstant(&prec->tsel) &&
        prec->tse == epicsTimeEventDeviceTime)
        dbGetTimeStamp(pinp, &prec->time);

    return status;
}

long init_record(struct dbCommon *pcommon)
{
    return 0;
}

long write_svi(svectoroutRecord* prec)
{
    long status = dbLinkDoLocked(&prec->out, writeLocked, 0);

    if (status == S_db_noLSET)
        status = writeLocked(&prec->out, 0);

    if (!status && !dbLinkIsConstant(&prec->out))
        prec->udf = FALSE;

    return status;
}

svectoroutdset devSVOSoft = {
    {5, NULL, NULL, &init_record, NULL},
    &write_svi
};

}
extern "C" {
epicsExportAddress(dset, devSVOSoft);
}
