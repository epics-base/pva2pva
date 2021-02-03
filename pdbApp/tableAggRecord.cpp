/*************************************************************************\
* Copyright (c) 2020 Michael Davidsaver
* EPICS BASE is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

#include <vector>
#include <string>

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
#include <epicsStdio.h>

#include <pv/pvData.h>
#include <pv/bitSet.h>
#include <pv/qsrv.h>

#define GEN_SIZE_OFFSET
#include <tableAggRecord.h>
#undef  GEN_SIZE_OFFSET
#include <epicsExport.h>

namespace pvd = epics::pvData;

namespace {

static const size_t MAX_COLS = 26;
static const size_t AMSG_LEN = 128;

static pvd::ScalarType to_pvd_type(epicsEnum16 type)
{
    switch(type) {
#define CASE(BASETYPE, PVATYPE, DBFTYPE, PVACODE) case DBF_##DBFTYPE: return pvd::pv##PVACODE;
#define CASE_SKIP_BOOL
#define CASE_REAL_INT64
#include <pv/typemap.h>
#undef CASE_SKIP_BOOL
#undef CASE_REAL_INT64
#undef CASE
    // not supporting ENUM or STRING
    default:
        throw std::runtime_error("not supported");
    }
}

template<typename T, typename R>
static T field_ptr(R *prec, size_t index) {
    dbFldDes **papFldDes = prec->rdes->papFldDes;
    return reinterpret_cast<T>(
        reinterpret_cast<char*>(prec) + papFldDes[index]->offset
    );
}

long initialize()
{
    return multiArray::initialize();
}

long init_record(struct dbCommon *pcommon, int pass)
{
    tableAggRecord *prec = reinterpret_cast<tableAggRecord*>(pcommon);
    tableAggdset *pdset = reinterpret_cast<tableAggdset*>(prec->dset);
    /* overall ordering
     * pass==0
     *   - check dset
     *   - multiArray::init_record(, 0)
     * pass==1
     *   - dset::init_record() -- calls add_column()
     *   - multiArray::init_record(, 1)
     */

    if (!pdset && pass == 0) {
        recGblRecordError(S_dev_noDSET, prec, "tableAgg: no DSET");
        return S_dev_noDSET;
    }

    if (pass == 1 && pdset->common.init_record) {
        long ret = pdset->common.init_record(pcommon);
        if(ret)
            return ret;
    }

    return multiArray::init_record(pcommon, pass);
}

void monitor(tableAggRecord *prec)
{
    int monitor_mask = recGblResetAlarms(prec);
    multiArray::monitor(prec, monitor_mask);
}

long process(struct dbCommon *pcommon)
{
    tableAggRecord *prec = reinterpret_cast<tableAggRecord*>(pcommon);
    tableAggdset *pdset = reinterpret_cast<tableAggdset*>(prec->dset);

    try {
        unsigned char pact = prec->pact;
        long status;

        if (!pdset || !pdset->read_tbl) {
            prec->pact = TRUE;
            recGblSetSevrMsg(prec, READ_ALARM, INVALID_ALARM, "no read_tbl");
            recGblRecordError(S_dev_missingSup, (void *)prec, "read_tbl");
            return S_dev_missingSup;

        } else if (!prec->val) {
            prec->pact = TRUE;
            recGblSetSevrMsg(prec, READ_ALARM, INVALID_ALARM, "no table");
            return S_dev_NoInit;
        }

        // Read the new value
        status = pdset->read_tbl(prec);

        // Check if device support set pact
        if (!pact && prec->pact)
            return 0;

        prec->pact = TRUE;
        recGblGetTimeStamp(prec);

        // Check event list
        monitor(prec);

        // Process the forward scan link record
        recGblFwdLink(prec);

        prec->pact = FALSE;
        return status;

    } catch(std::exception& e) {
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
    return multiArray::put_vfield(paddr, p);
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

rset tableAggRSET = {
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
    const char *fname = static_cast<const char*>(raw);
    dbCommon *prec = pinp->precord;

    pvd::shared_vector<const void> arr;

    VSharedVector ival;
    ival.vtype = &vfSharedVector;
    ival.value = &arr;

    long status = dbGetLink(pinp, DBR_VFIELD, &ival, 0, 0);

    if (status == S_db_badDbrtype && !dbLinkIsConstant(pinp)) {
        // Fallback to reading (and copying) DBF array
        status = 0;

        int dbf = dbGetLinkDBFtype(pinp);

        pvd::ScalarType stype;

        try {
            stype = to_pvd_type(dbf);
        } catch (std::runtime_error &) {
            recGblSetSevrMsg(prec, READ_ALARM, INVALID_ALARM, "Bad DBF %d", dbf);
            return S_db_badDbrtype;
        }

        long nelem = 0;

        if (dbf == -1 || (status = dbGetNelements(pinp, &nelem)))
            return status;

        size_t esize = dbValueSize(dbf);

        try {
            pvd::shared_vector<char> temp(nelem*esize);

            status = dbGetLink(pinp, dbf, temp.data(), 0, &nelem);

            if (!status) {
                temp.resize(nelem*esize);
                arr = pvd::static_shared_vector_cast<const void>(pvd::freeze(temp));
                arr.set_original_type(stype);
            }
        } catch(std::exception& e) {
            errlogPrintf("%s: Error during alloc/copy of DBF -> PVD: %s\n", prec->name, e.what());
            recGblSetSevrMsg(prec, READ_ALARM, INVALID_ALARM, "DBF CPY %s", e.what());
            return S_rec_outMem; /* this is likely an allocation error */
        }
    }

    multiArray::ColMeta meta;
    char amsg[AMSG_LEN] = {};

    if ((status = dbGetTimeStampTag(pinp, &meta.time, &meta.utag)))
        errlogPrintf("%s [%s]: failed to get timestamp\n", prec->name, fname);

    if ((status = dbGetAlarmMsg(pinp, NULL, &meta.sevr, amsg, sizeof(amsg))))
        errlogPrintf("%s [%s]: failed to get alarm\n", prec->name, fname);

    meta.amsg = amsg;
    multiArray::set_column(prec, fname, arr, &meta);

    return 0;
}

long init_record_soft(struct dbCommon *pcommon)
{
    tableAggRecord *prec = reinterpret_cast<tableAggRecord*>(pcommon);

    for (size_t i = 0; i < MAX_COLS; ++i) {
        // Assume NUL-terminated strings
        const char *fname = field_ptr<const char*>(prec, tableAggRecordFNAA + i);
        const char *label = field_ptr<const char*>(prec, tableAggRecordLABA + i);
        epicsEnum16 *type = field_ptr<epicsEnum16*>(prec, tableAggRecordFTA + i);

        // Stop at first unnamed column
        if (!strlen(fname))
            break;

        multiArray::add_column(pcommon, fname, label, to_pvd_type(*type));
    }

    return 0;
}

long read_tbl_soft(tableAggRecord* prec)
{
    // Read data + metadata from links
    for (size_t i = 0; i < MAX_COLS; ++i) {
        const char *fname = field_ptr<const char*>(prec, tableAggRecordFNAA + i);
        struct link *pinp = field_ptr<struct link*>(prec, tableAggRecordINPA + i);

        // Stop at first unnamed column
        if (!strlen(fname))
            break;

        long status = dbLinkDoLocked(pinp, readLocked, (void*)fname);
        if (status == S_db_noLSET)
            status = readLocked(pinp, (void*)fname);

        if (!status)
            prec->udf = FALSE;

        if (status)
            return status;
    }
    return 0;
}

tableAggdset devTASoft = {
    {5, NULL, NULL, &init_record_soft, NULL},
    &read_tbl_soft
};

} // namespace

extern "C" {
epicsExportAddress(rset,tableAggRSET);
epicsExportAddress(dset,devTASoft);
}
