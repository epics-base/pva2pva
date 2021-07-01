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

// include by pvstructinRecord.h
#include "epicsTypes.h"
#include "link.h"
#include "epicsMutex.h"
#include "ellLib.h"
#include "devSup.h"
#include "epicsTime.h"

#include "pvstructinRecord.h"
#include <epicsExport.h>

namespace {

namespace pvd = epics::pvData;

long readLocked(struct link *pinp, void *raw)
{
    const bool* doload = static_cast<const bool*>(raw);
    pvstructinRecord *prec = (pvstructinRecord *) pinp->precord;

    VSharedPVStructure ival;
    ival.vtype = &vfPVStructure;
    ival.value = &prec->val;
    ival.changed = &prec->chg;

    long status = *doload ? dbLoadLink(pinp, DBR_VFIELD, &ival) : dbGetLink(pinp, DBR_VFIELD, &ival, 0, 0);

    if (status) return status;

    if (dbLinkIsConstant(&prec->tsel) &&
        prec->tse == epicsTimeEventDeviceTime)
        dbGetTimeStamp(pinp, &prec->time);

    return status;
}

long init_record(struct dbCommon *pcommon)
{
    pvstructinRecord *prec = (pvstructinRecord *)pcommon;
    bool doload = true;

    if (readLocked(&prec->inp, &doload)) {
        prec->udf = FALSE;
    }

    return 0;
}

long read_svi(pvstructinRecord* prec)
{
    bool doload = false;
    long status = dbLinkDoLocked(&prec->inp, readLocked, &doload);

    if (status == S_db_noLSET)
        status = readLocked(&prec->inp, &doload);

    if (!status && !dbLinkIsConstant(&prec->inp))
        prec->udf = FALSE;

    return status;
}

pvstructindset devPVSISoft = {
    {5, NULL, NULL, &init_record, NULL},
    &read_svi
};

}
extern "C" {
epicsExportAddress(dset, devPVSISoft);
}
