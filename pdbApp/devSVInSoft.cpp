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

// include by svectorinRecord.h
#include "epicsTypes.h"
#include "link.h"
#include "epicsMutex.h"
#include "ellLib.h"
#include "devSup.h"
#include "epicsTime.h"

#include "svectorinRecord.h"
#include <epicsExport.h>

namespace {

namespace pvd = epics::pvData;

long readLocked(struct link *pinp, void *raw)
{
    const bool* doload = static_cast<const bool*>(raw);
    svectorinRecord *prec = (svectorinRecord *) pinp->precord;

    VSharedVector ival;
    ival.vtype = &vfSharedVector;
    ival.value = &prec->val;

    long status = *doload ? dbLoadLink(pinp, DBR_VFIELD, &ival) : dbGetLink(pinp, DBR_VFIELD, &ival, 0, 0);

    if (status==S_db_badDbrtype) {
        status=0;

        size_t esize = dbValueSize(prec->ftvl);
        pvd::shared_vector<char> temp(prec->nelm*esize);
        long nReq = prec->nelm;

        if(!status && !(status = *doload ? dbLoadLinkArray(pinp, prec->ftvl, temp.data(), &nReq) : dbGetLink(pinp, prec->ftvl, temp.data(), 0, &nReq)) && nReq>=0) {
            if(prec->ftvl==DBR_STRING) {
                // translate char[] to std::string
                pvd::shared_vector<std::string> scratch(nReq);
                for(size_t i=0u, N=scratch.size(); i<N; i++) {
                    char* src = &temp[i*esize];
                    src[esize-1] = '\0';
                    scratch[i] = src;
                }
                prec->val = pvd::static_shared_vector_cast<const void>(pvd::freeze(scratch));
            } else {
                temp.resize(nReq*esize);
                prec->val = pvd::static_shared_vector_cast<const void>(pvd::freeze(temp));
                prec->val.set_original_type(prec->stvl);
            }

        } else if(!status) {
            status = -1;
        }
    }
    if (status) return status;

    if (dbLinkIsConstant(&prec->tsel) &&
        prec->tse == epicsTimeEventDeviceTime)
        dbGetTimeStamp(pinp, &prec->time);

    return status;
}

long init_record(struct dbCommon *pcommon)
{
    svectorinRecord *prec = (svectorinRecord *)pcommon;
    bool doload = true;

    if (readLocked(&prec->inp, &doload)) {
        prec->udf = FALSE;
    }

    return 0;
}

long read_svi(svectorinRecord* prec)
{
    bool doload = false;
    long status = dbLinkDoLocked(&prec->inp, readLocked, &doload);

    if (status == S_db_noLSET)
        status = readLocked(&prec->inp, &doload);

    if (!status && !dbLinkIsConstant(&prec->inp))
        prec->udf = FALSE;

    return status;
}

svectorindset devSVISoft = {
    {5, NULL, NULL, &init_record, NULL},
    &read_svi
};

}
extern "C" {
epicsExportAddress(dset, devSVISoft);
}
