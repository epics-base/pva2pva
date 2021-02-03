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
#include <columnarinRecord.h>
#undef  GEN_SIZE_OFFSET
#include <epicsExport.h>

namespace pvd = epics::pvData;

namespace {

long initialize()
{
    return multiArray::initialize();
}

long init_record(struct dbCommon *pcommon, int pass)
{
    columnarinRecord *prec = (columnarinRecord*)pcommon;
    columnarindset *pdset = (columnarindset *)(prec->dset);
    /* overall ordering
     * pass==0
     *   - check dset
     *   - multiArray::init_record(, 0)
     * pass==1
     *   - dset::init_record() -- calls add_column()
     *   - multiArray::init_record(, 1)
     */

    if (!pdset && pass==0) {
        recGblRecordError(S_dev_noDSET, prec, "columnarin: no DSET");
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

long readValue(columnarinRecord *prec,
               columnarindset *pdset)
{
    return pdset->read_tbl(prec);
}

void monitor(columnarinRecord *prec)
{
    int monitor_mask = recGblResetAlarms(prec);

    multiArray::monitor(prec, monitor_mask);
}

long process(struct dbCommon *pcommon)
{
    columnarinRecord *prec = (columnarinRecord*)pcommon;
    columnarindset *pdset = (columnarindset *)(prec->dset);
    try {

        unsigned char    pact=prec->pact;
        long status;

        if( (pdset==NULL) || (pdset->read_tbl==NULL) ) {
            prec->pact=TRUE;
            recGblSetSevrMsg(prec, READ_ALARM, INVALID_ALARM, "no read_tbl");
            recGblRecordError(S_dev_missingSup,(void *)prec,"read_tbl");
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

rset columnarinRSET = {
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
    columnarinRecord *prec = (columnarinRecord *) pinp->precord;

    VSharedPVStructure ival;
    ival.vtype = &vfPVStructure;
    ival.value = &prec->val;
    ival.changed = &prec->vld;

    long status = dbGetLink(pinp, DBR_VFIELD, &ival, 0, 0);

    if (status) return status;

    if (dbLinkIsConstant(&prec->tsel) &&
        prec->tse == epicsTimeEventDeviceTime)
        dbGetTimeStamp(pinp, &prec->time);

    return status;
}

long read_tbl_soft(columnarinRecord* prec)
{
    long status = dbLinkDoLocked(&prec->inp, readLocked, 0);

    if (status == S_db_noLSET)
        status = readLocked(&prec->inp, 0);

    if (!status && !dbLinkIsConstant(&prec->inp))
        prec->udf = FALSE;

    return status;
}

columnarindset devCOLISoft = {
    {5, NULL, NULL, NULL, NULL},
    &read_tbl_soft
};


} // namespace

extern "C" {
epicsExportAddress(rset,columnarinRSET);
epicsExportAddress(dset, devCOLISoft);
}