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

#define GEN_SIZE_OFFSET
#include <statBinRecord.h>
#undef  GEN_SIZE_OFFSET
#include <epicsExport.h>

namespace pvd = epics::pvData;

namespace statBin {

struct RPvt {
    pvd::PVStructurePtr root;
    pvd::BitSet valid, changed;

    template<typename PVD>
    inline
    std::tr1::shared_ptr<PVD> assign(const char* name) {
        std::tr1::shared_ptr<PVD> fld(root->getSubFieldT<PVD>(name));
        changed.set(fld->getFieldOffset());
        return fld;
    }
};

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

    prec->rpvt->assign<pvd::PVDoubleArray>("idx.value")->replace(pvd::freeze(idx));
    prec->rpvt->assign<pvd::PVDoubleArray>("min.value")->replace(pvd::freeze(min));
    prec->rpvt->assign<pvd::PVDoubleArray>("max.value")->replace(pvd::freeze(max));
    prec->rpvt->assign<pvd::PVDoubleArray>("mean.value")->replace(pvd::freeze(mean));
    prec->rpvt->assign<pvd::PVDoubleArray>("std.value")->replace(pvd::freeze(std));

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

ELLLIST vfPVStructureList = ELLLIST_INIT;
VFieldTypeNode vfPVStructureNode[2];

long initialize()
{
    vfPVStructureNode[0].vtype = &vfStructure;
    ellAdd(&vfPVStructureList, &vfPVStructureNode[0].node);
    vfPVStructureNode[1].vtype = &vfPVStructure;
    ellAdd(&vfPVStructureList, &vfPVStructureNode[1].node);
    return 0;
}

long init_record(struct dbCommon *pcommon, int pass)
{
    statBinRecord *prec = (statBinRecord*)pcommon;
    statBindset *pdset = (statBindset *)(prec->dset);
    try {

        if (!pdset) {
            if(pass==0)
                recGblRecordError(S_dev_noDSET, prec, "statBin: no DSET");
            return S_dev_noDSET;
        }

        if(pass==0) {
            prec->rpvt = new RPvt;

        } else { // pass==1

            const pvd::StandardFieldPtr& sfld(pvd::getStandardField());

            pvd::StructureConstPtr elem(sfld->scalarArray(pvd::pvDouble, "alarm,timeStamp"));

            prec->rpvt->root = pvd::FieldBuilder::begin()
                    ->add("idx", elem)
                    ->add("mean", elem)
                    ->add("std", elem)
                    ->add("min", elem)
                    ->add("max", elem)
                    ->createStructure()->build();

            if(pdset->common.init_record) {
                long ret = pdset->common.init_record(pcommon);
                if(ret)
                    return ret;
            }

            prec->rpvt->valid = prec->rpvt->changed;
        }

        return 0;
    }catch(std::exception& e){
        fprintf(stderr, "%s: init_record error: %s\n", prec->name, e.what());
        return -1;
    }
}

long readValue(statBinRecord *prec,
               statBindset *pdset)
{
    prec->rpvt->changed.clear();

    long ret = pdset->read_arr(prec);

    return  ret;
}

void monitor(statBinRecord *prec)
{
    int monitor_mask = recGblResetAlarms(prec);

    monitor_mask |= DBE_VALUE | DBE_LOG;

    prec->rpvt->assign<pvd::PVScalar>("idx.timeStamp.secondsPastEpoch")->putFrom<pvd::uint32>(prec->time.secPastEpoch + POSIX_TIME_AT_EPICS_EPOCH);
    prec->rpvt->assign<pvd::PVScalar>("min.timeStamp.secondsPastEpoch")->putFrom<pvd::uint32>(prec->time.secPastEpoch + POSIX_TIME_AT_EPICS_EPOCH);
    prec->rpvt->assign<pvd::PVScalar>("max.timeStamp.secondsPastEpoch")->putFrom<pvd::uint32>(prec->time.secPastEpoch + POSIX_TIME_AT_EPICS_EPOCH);
    prec->rpvt->assign<pvd::PVScalar>("mean.timeStamp.secondsPastEpoch")->putFrom<pvd::uint32>(prec->time.secPastEpoch + POSIX_TIME_AT_EPICS_EPOCH);
    prec->rpvt->assign<pvd::PVScalar>("std.timeStamp.secondsPastEpoch")->putFrom<pvd::uint32>(prec->time.secPastEpoch + POSIX_TIME_AT_EPICS_EPOCH);

    prec->rpvt->assign<pvd::PVScalar>("idx.timeStamp.nanoseconds")->putFrom<pvd::uint32>(prec->time.nsec);
    prec->rpvt->assign<pvd::PVScalar>("min.timeStamp.nanoseconds")->putFrom<pvd::uint32>(prec->time.nsec);
    prec->rpvt->assign<pvd::PVScalar>("max.timeStamp.nanoseconds")->putFrom<pvd::uint32>(prec->time.nsec);
    prec->rpvt->assign<pvd::PVScalar>("mean.timeStamp.nanoseconds")->putFrom<pvd::uint32>(prec->time.nsec);
    prec->rpvt->assign<pvd::PVScalar>("std.timeStamp.nanoseconds")->putFrom<pvd::uint32>(prec->time.nsec);

    prec->rpvt->assign<pvd::PVScalar>("idx.alarm.severity")->putFrom<pvd::uint32>(prec->sevr);
    prec->rpvt->assign<pvd::PVScalar>("min.alarm.severity")->putFrom<pvd::uint32>(prec->sevr);
    prec->rpvt->assign<pvd::PVScalar>("max.alarm.severity")->putFrom<pvd::uint32>(prec->sevr);
    prec->rpvt->assign<pvd::PVScalar>("mean.alarm.severity")->putFrom<pvd::uint32>(prec->sevr);
    prec->rpvt->assign<pvd::PVScalar>("std.alarm.severity")->putFrom<pvd::uint32>(prec->sevr);

    prec->rpvt->assign<pvd::PVScalar>("idx.alarm.status")->putFrom<pvd::uint32>(prec->stat ? 1 : 0);
    prec->rpvt->assign<pvd::PVScalar>("min.alarm.status")->putFrom<pvd::uint32>(prec->stat ? 1 : 0);
    prec->rpvt->assign<pvd::PVScalar>("max.alarm.status")->putFrom<pvd::uint32>(prec->stat ? 1 : 0);
    prec->rpvt->assign<pvd::PVScalar>("mean.alarm.status")->putFrom<pvd::uint32>(prec->stat ? 1 : 0);
    prec->rpvt->assign<pvd::PVScalar>("std.alarm.status")->putFrom<pvd::uint32>(prec->stat ? 1 : 0);

    std::string amsg(prec->amsg);
    prec->rpvt->assign<pvd::PVString>("idx.alarm.message")->put(amsg);
    prec->rpvt->assign<pvd::PVString>("min.alarm.message")->put(amsg);
    prec->rpvt->assign<pvd::PVString>("max.alarm.message")->put(amsg);
    prec->rpvt->assign<pvd::PVString>("mean.alarm.message")->put(amsg);
    prec->rpvt->assign<pvd::PVString>("std.alarm.message")->put(amsg);

    if (monitor_mask) {
        db_post_events(prec, &prec->val, monitor_mask);
    }

    prec->rpvt->valid |= prec->rpvt->changed;
}

long process(struct dbCommon *pcommon)
{
    statBinRecord *prec = (statBinRecord*)pcommon;
    statBindset *pdset = (statBindset *)(prec->dset);
    try {

        unsigned char    pact=prec->pact;
        long status;

        if( (pdset==NULL) || (pdset->read_arr==NULL) ) {
            prec->pact=TRUE;
            recGblSetSevrMsg(prec, READ_ALARM, INVALID_ALARM, "no read_arr");
            recGblRecordError(S_dev_missingSup,(void *)prec,"read_arr");
            return S_dev_missingSup;

        } else if(!prec->rpvt->root) {
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
    // for VAL

    // we don't provide a valid DBR buffer
    paddr->ro = 1;
    // arbitrary limit
    paddr->no_elements = 1;

    paddr->field_type = DBF_NOACCESS;

    if(dbGetFieldIndex(paddr)==pvstructinRecordVAL) {
        // we provide vfield access
        paddr->vfields = &vfPVStructureList;
    }

    return 0;
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
    statBinRecord *prec = (statBinRecord*)paddr->precord;

    if(!prec->rpvt->root)
        return S_db_notInit;

    if(p->vtype==&vfPVStructure) {
        VSharedPVStructure *pstr = (VSharedPVStructure*)p;
        if(dbGetFieldIndex(paddr)==pvstructinRecordVAL) {
            if(!*pstr->value)
                return S_db_notInit;
            (*pstr->value)->copy(*prec->rpvt->root);
            *pstr->changed = prec->rpvt->valid; // TODO: distinguish initial from update
            return 0;
        }

    } else if(p->vtype==&vfStructure) {
        VSharedStructure *pstr = (VSharedStructure*)p;
        if(dbGetFieldIndex(paddr)==pvstructinRecordVAL) {
            *pstr->value = prec->rpvt->root->getStructure();
            return 0;
        }
    }
    return S_db_badChoice;
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

long readLocked(struct link *pinp, void *raw)
{
    const bool* doload = static_cast<const bool*>(raw);
    statBinRecord *prec = (statBinRecord *) pinp->precord;

    pvd::shared_vector<const void> arr;

    VSharedVector ival;
    ival.vtype = &vfSharedVector;
    ival.value = &arr;

    long status = *doload ? dbLoadLink(pinp, DBR_VFIELD, &ival) : dbGetLink(pinp, DBR_VFIELD, &ival, 0, 0);

    if (status==S_db_badDbrtype) {
        // TODO: fallback to alloc and copy
    }

    if (!status)
        status = statBin::consume(prec, arr);

    if (status) return status;

    if (dbLinkIsConstant(&prec->tsel) &&
        prec->tse == epicsTimeEventDeviceTime)
        dbGetTimeStamp(pinp, &prec->time);

    return status;
}

long init_record_soft(struct dbCommon *pcommon)
{
    statBinRecord *prec = (statBinRecord *)pcommon;
    bool doload = true;

    if (readLocked(&prec->inp, &doload)) {
        prec->udf = FALSE;
    }
    return 0;
}

long read_arr_soft(statBinRecord* prec)
{
    bool doload = false;
    long status = dbLinkDoLocked(&prec->inp, readLocked, &doload);

    if (status == S_db_noLSET)
        status = readLocked(&prec->inp, &doload);

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
