/*************************************************************************\
* Copyright (c) 2020 Michael Davidsaver
* EPICS BASE is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

#ifndef USE_TYPED_RSET
#  define USE_TYPED_RSET
#endif
#ifndef USE_TYPED_DSET
#  define USE_TYPED_DSET
#endif

#include <pv/standardField.h>

#include <dbAccess.h>
#include <recSup.h>
#include <recGbl.h>
#include <dbEvent.h>
#include <errlog.h>
#include <alarm.h>
#include <epicsMath.h>

// include by statBinRecord.h
#include "epicsTypes.h"
#include "link.h"
#include "epicsMutex.h"
#include "ellLib.h"
#include "devSup.h"
#include "epicsTime.h"

#include "pvstructinRecord.h"
#include "svectorinRecord.h"

#include <multiArrayCommon.h>

#define GEN_SIZE_OFFSET
#include <statBinRecord.h>
#undef  GEN_SIZE_OFFSET
#include <epicsExport.h>

namespace pvd = epics::pvData;

namespace statBin {

long consume(struct statBinRecord *prec, const epics::pvData::shared_vector<const void>& arr)
{
    short dbf;
    size_t esize;
    switch(arr.original_type()) {
#define CASE(BASETYPE, PVATYPE, DBFTYPE, PVACODE) case pvd::pv##PVACODE: esize = sizeof(epics##BASETYPE); dbf = DBF_##DBFTYPE; break;
#define CASE_REAL_INT64
#define CASE_SKIP_BOOL
#include "pv/typemap.h"
#undef CASE_REAL_INT64
#undef CASE_SKIP_BOOL
#undef CASE
    default:
        return S_db_badDbrtype;
    }

    return statBinConsume(prec, dbf, arr.data(), arr.size()/esize);
}

} // namespace statBin

namespace {
template<typename T>
long statBinConsumeIt(struct statBinRecord *prec, const void* raw, size_t nelem)
{
    if(!prec->rpvt)
        return S_db_notInit;

    const T* arr = static_cast<const T*>(raw);
    size_t dec = prec->dec ? prec->dec : nelem;

    size_t nbins = nelem/dec;
    if(nelem%dec)
        nbins++;

    pvd::shared_vector<double> mean(nbins),
                               std(nbins),
                               min(nbins),
                               max(nbins),
                               idx(nbins);

    for(size_t i=0, n=0u; n<nbins && i<nelem; n++) {
        idx[n] = i;

        double e = arr[i],
               bmin = e,
               bmax = e,
               bsum = e,
               bsum2= e*e;
        size_t cnt = 1u;
        i++;

        for(; cnt<dec && i<nelem; i++, cnt++) {
            e = arr[i];

            if(bmin > e)
                bmin = e;
            if(bmax < e)
                bmax = e;
            bsum += e;
            bsum2+= e*e;
        }

        min[n] = bmin;
        max[n] = bmax;
        mean[n] = bsum/cnt;
        std[n] = sqrt(bsum2/cnt - mean[n]*mean[n]);
    }

    multiArray::set_column(prec, "idx", pvd::static_shared_vector_cast<const void>(pvd::freeze(idx)));
    multiArray::set_column(prec, "min", pvd::static_shared_vector_cast<const void>(pvd::freeze(min)));
    multiArray::set_column(prec, "max", pvd::static_shared_vector_cast<const void>(pvd::freeze(max)));
    multiArray::set_column(prec, "mean", pvd::static_shared_vector_cast<const void>(pvd::freeze(mean)));
    multiArray::set_column(prec, "std", pvd::static_shared_vector_cast<const void>(pvd::freeze(std)));

    return -1;
}
} // namespace

long statBinConsume(struct statBinRecord *prec, short dbf, const void* arr, size_t nelem)
{
    switch (dbf) {
#define CASE(BASETYPE, PVATYPE, DBFTYPE, PVACODE) case DBF_##DBFTYPE: return statBinConsumeIt<epics##BASETYPE>(prec, arr, nelem);
#define CASE_REAL_INT64
#define CASE_SKIP_BOOL
#include "pv/typemap.h"
#undef CASE_REAL_INT64
#undef CASE_SKIP_BOOL
#undef CASE
    default:
        return S_db_badDbrtype;
    }
}

namespace  {
using namespace statBin;

long initialize()
{
    return multiArray::initialize();
}

long init_record(struct dbCommon *pcommon, int pass)
{
    statBinRecord *prec = (statBinRecord*)pcommon;
    statBindset *pdset = (statBindset *)(prec->dset);
    /* overall ordering
     * pass==0
     *   - check dset
     *   - multiArray::init_record(, 0)
     * pass==1
     *   - dset::init_record() -- calls add_column()
     *   - multiArray::init_record(, 1)
     */

    if (!pdset && pass==0) {
        recGblRecordError(S_dev_noDSET, prec, "statBin: no DSET");
        return S_dev_noDSET;
    }

    if(pass==1 && pdset->common.init_record) {
        long ret = pdset->common.init_record(pcommon);
        if(ret)
            return ret;
    }

    long status = multiArray::init_record(pcommon, pass);

    return status;
}

long readValue(statBinRecord *prec,
               statBindset *pdset)
{
    return pdset->read_stats(prec);
}

void monitor(statBinRecord *prec)
{
    int monitor_mask = recGblResetAlarms(prec);

    multiArray::monitor(prec, monitor_mask | DBE_VALUE | DBE_ARCHIVE);
}

long process(struct dbCommon *pcommon)
{
    statBinRecord *prec = (statBinRecord*)pcommon;
    statBindset *pdset = (statBindset *)(prec->dset);
    try {

        unsigned char    pact=prec->pact;
        long status;

        if( (pdset==NULL) || (pdset->read_stats==NULL) ) {
            prec->pact=TRUE;
            recGblSetSevrMsg(prec, READ_ALARM, INVALID_ALARM, "no read_stats");
            recGblRecordError(S_dev_missingSup,(void *)prec,"read_stats");
            return S_dev_missingSup;

        } else if(!prec->val) {
            prec->pact=TRUE;
            recGblSetSevrMsg(prec, READ_ALARM, INVALID_ALARM, "no table");
            return S_dev_NoInit;
        }

        status=readValue(prec, pdset); /* read the new value */
        /* check if device support set pact */
        if ( !pact && prec->pact ) return(0);

        prec->pact = TRUE;
        recGblGetTimeStamp(prec);

        /* check event list */
        monitor(prec);
        /* process the forward scan link record */
        recGblFwdLink(prec);

        prec->pact=FALSE;
        return status;
    }catch(std::exception& e){
        fprintf(stderr, "%s: process error: %s\n", prec->name, e.what());
        return -1;
    }
}

long cvt_dbaddr(DBADDR *paddr)
{
    return multiArray::cvt_dbaddr(paddr);
}

long get_array_info(DBADDR *paddr, long *no_elements, long *offset)
{
    return S_db_badField;
}

long put_array_info(DBADDR *paddr, long nNew)
{
    return S_db_noMod;
}

long get_vfield(struct dbAddr *paddr, struct VField *p)
{
    return multiArray::get_vfield(paddr, p);
}

long put_vfield(struct dbAddr *paddr, const struct VField *p)
{
    return S_db_noMod;
}

#define report NULL
#define special NULL
#define get_value NULL
#define get_units NULL
#define get_precision NULL
#define get_enum_str NULL
#define get_enum_strs NULL
#define put_enum_str NULL
#define get_graphic_double NULL
#define get_control_double NULL
#define get_alarm_double NULL

rset statBinRSET={
    RSETNUMBER,
    report,
    initialize,
    init_record,
    process,
    special,
    get_value,
    cvt_dbaddr,
    get_array_info,
    put_array_info,
    get_units,
    get_precision,
    get_enum_str,
    get_enum_strs,
    put_enum_str,
    get_graphic_double,
    get_control_double,
    get_alarm_double,
    get_vfield,
    put_vfield,
};

// Soft Channel (default) device support

long readLocked(struct link *pinp, void *raw)
{
    statBinRecord *prec = (statBinRecord *) pinp->precord;

    pvd::shared_vector<const void> arr;

    VSharedVector ival;
    ival.vtype = &vfSharedVector;
    ival.value = &arr;

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
                arr = pvd::static_shared_vector_cast<const void>(pvd::freeze(temp));
                arr.set_original_type(stype);
            }
        } catch(std::exception& e) {
            errlogPrintf("%s: Error during alloc/copy of DBF -> PVD: %s\n", prec->name, e.what());
            recGblSetSevrMsg(prec, READ_ALARM, INVALID_ALARM, "DBF CPY %s", e.what());
            status = S_rec_outMem; /* this is likely an allocation error */
        }
    }

    if (!status)
        status = statBin::consume(prec, arr);

    if (status) return status;

    if (dbLinkIsConstant(&prec->tsel) &&
        prec->tse == epicsTimeEventDeviceTime)
        dbGetTimeStamp(pinp, &prec->time);

    return status;
}

multiArray::Entry columns[] = {
    {"idx", "1st Index", pvd::pvDouble},
    {"min", "Min", pvd::pvDouble},
    {"max", "Max", pvd::pvDouble},
    {"mean","Mean", pvd::pvDouble},
    {"std", "StdDev", pvd::pvDouble},
    {0},
};

long init_record_soft(struct dbCommon *pcommon)
{
    statBinRecord *prec = (statBinRecord *)pcommon;

    multiArray::add_columns(prec, columns);

    return 0;
}

long read_arr_soft(statBinRecord* prec)
{
    long status = dbLinkDoLocked(&prec->inp, readLocked, 0);

    if (status == S_db_noLSET)
        status = readLocked(&prec->inp, 0);

    if (!status && !dbLinkIsConstant(&prec->inp))
        prec->udf = FALSE;

    return status;
}

statBindset devSBSoft = {
    {5, NULL, NULL, &init_record_soft, NULL},
    &read_arr_soft
};

} // namespace

extern "C" {
epicsExportAddress(rset,statBinRSET);
epicsExportAddress(dset, devSBSoft);
}
