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

#include <dbAccess.h>
#include <recSup.h>
#include <recGbl.h>
#include <dbEvent.h>
#include <errlog.h>
#include <alarm.h>

// include by svectorinRecord.h
#include "epicsTypes.h"
#include "link.h"
#include "epicsMutex.h"
#include "ellLib.h"
#include "devSup.h"
#include "epicsTime.h"

#include <pv/qsrv.h>

#define GEN_SIZE_OFFSET
#include <svectorinRecord.h>
#undef  GEN_SIZE_OFFSET
#include <epicsExport.h>

namespace  {

namespace pvd = epics::pvData;

ELLLIST vfSharedVectorList = ELLLIST_INIT;
VFieldTypeNode vfSharedVectorNode;

long initialize()
{
    vfSharedVectorNode.vtype = &vfSharedVector;
    ellAdd(&vfSharedVectorList, &vfSharedVectorNode.node);
    return 0;
}

long type_check(svectorinRecord *prec)
{
    if(prec->val.empty() || prec->val.original_type()==prec->stvl)
        return 0;

    errlogPrintf("%s error: device support attempts type change.  Clear VAL\n", prec->name);
    (void)recGblSetSevrMsg(prec, READ_ALARM, INVALID_ALARM, "STVL type mismatch");

    prec->val.clear();

    return S_db_badDbrtype;
}

long init_record(struct dbCommon *pcommon, int pass)
{
    svectorinRecord *prec = (svectorinRecord*)pcommon;
    svectorindset *pdset = (svectorindset *)(prec->dset);

    if(pass==0) {
        new (&prec->val) pvd::shared_vector<const void>();
        new (&prec->oval) pvd::shared_vector<const void>();

        if (!pdset) {
            recGblSetSevrMsg(prec, READ_ALARM, INVALID_ALARM, "no DSET");
            recGblRecordError(S_dev_noDSET, prec, "svectorin: no DSET");
            return S_dev_noDSET;
        }

        switch(prec->ftvl) {
    #define CASE(BASETYPE, PVATYPE, DBFTYPE, PVACODE) case DBF_##DBFTYPE: prec->stvl = pvd::pv##PVACODE; break;
    #define CASE_SKIP_BOOL
    #define CASE_REAL_INT64
    #include <pv/typemap.h>
    #undef CASE_SKIP_BOOL
    #undef CASE_REAL_INT64
    #undef CASE
        // not supporting ENUM or STRING
        default:
            prec->stvl = (pvd::ScalarType)-1;
            recGblSetSevrMsg(prec, READ_ALARM, INVALID_ALARM, "Bad FTVL");
            recGblRecordError(S_db_badDbrtype, prec, "svectorin: bad FTVL");
            return S_db_badDbrtype;
        }

    } else { // pass==1

        if(pdset->common.init_record) {
            long ret = pdset->common.init_record(pcommon);
            if(!ret)
                ret = type_check(prec);
            if(ret)
                return ret;

            prec->oval = prec->val;
        }
    }
    return 0;
}

long readValue(svectorinRecord *prec,
               svectorindset *pdset)
{
    long ret = pdset->read_sharedvector(prec);

    if(!ret)
        ret = type_check(prec);

    return  ret;
}

void monitor(svectorinRecord *prec)
{
    int monitor_mask = recGblResetAlarms(prec);

    if ((prec->oval.dataPtr()!=prec->val.dataPtr())
            || (prec->oval.dataOffset()!=prec->val.dataOffset())
            || (prec->oval.size()!=prec->val.size())) {
        monitor_mask |= DBE_VALUE | DBE_LOG;
        prec->oval = prec->val;
    }

    if (monitor_mask) {
        db_post_events(prec, &prec->val, monitor_mask);
        db_post_events(prec, &prec->oval, monitor_mask);
    }
}

long process(struct dbCommon *pcommon)
{
    svectorinRecord *prec = (svectorinRecord*)pcommon;
    svectorindset *pdset = (svectorindset *)(prec->dset);
    unsigned char    pact=prec->pact;
    long status;

    if( (pdset==NULL) || (pdset->read_sharedvector==NULL) ) {
        prec->pact=TRUE;
        recGblSetSevrMsg(prec, READ_ALARM, INVALID_ALARM, "no read_svectorin");
        recGblRecordError(S_dev_missingSup,(void *)prec,"read_svectorin");
        return S_dev_missingSup;

    } else if(prec->stvl == (pvd::ScalarType)-1) {
        recGblSetSevrMsg(prec, READ_ALARM, INVALID_ALARM, "Bad FTVL");
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
}

long cvt_dbaddr(DBADDR *paddr)
{
    svectorinRecord *prec = (svectorinRecord*)paddr->precord;
    // for both VAL and OVAL

    // we don't insist devsup allocate the required NELM
    paddr->ro = 1;
    // we provide vfield access
    paddr->vfields = &vfSharedVectorList;
    // arbitrary limit
    paddr->no_elements = prec->nelm;

    paddr->field_type = paddr->dbr_field_type = prec->ftvl;
    paddr->field_size = 1;


    return 0;
}

long get_array_info(DBADDR *paddr, long *no_elements, long *offset)
{
    svectorinRecord *prec = (svectorinRecord*)paddr->precord;

    if(prec->stvl == (pvd::ScalarType)-1) {
        return S_db_badDbrtype;
    }

    size_t esize = pvd::ScalarTypeFunc::elementSize(prec->stvl);

    *offset = 0;
    if(dbGetFieldIndex(paddr)==svectorinRecordVAL) {
        paddr->pfield = (void*)prec->val.data();
        *no_elements = prec->val.size()/esize;
        if(paddr->no_elements < *no_elements)
            *no_elements = paddr->no_elements;
        return 0;

    } else if(dbGetFieldIndex(paddr)==svectorinRecordOVAL) {
        paddr->pfield = (void*)prec->oval.data();
        *no_elements = prec->oval.size()/esize;
        return 0;
    }
    return S_db_badField;
}

long put_array_info(DBADDR *paddr, long nNew)
{
    return S_db_noMod;
}

long get_vfield(struct dbAddr *paddr, struct VField *p)
{
    svectorinRecord *prec = (svectorinRecord*)paddr->precord;

    if(p->vtype==&vfSharedVector) {
        VSharedVector *pstr = (VSharedVector*)p;
        if(dbGetFieldIndex(paddr)==svectorinRecordVAL) {
            *pstr->value = prec->val;
            return 0;
        } else if(dbGetFieldIndex(paddr)==svectorinRecordOVAL) {
            *pstr->value = prec->oval;
            return 0;
        }
    }
    return S_db_badChoice;
}

long put_vfield(struct dbAddr *paddr, const struct VField *p)
{
    svectorinRecord *prec = (svectorinRecord*)paddr->precord;

    if(p->vtype==&vfSharedVector) {
        const VSharedVector *pstr = (const VSharedVector*)p;
        if(dbGetFieldIndex(paddr)==svectorinRecordVAL) {
            prec->val = *pstr->value;
            return 0;
        } else if(dbGetFieldIndex(paddr)==svectorinRecordOVAL) {
            prec->oval = *pstr->value;
            return 0;
        }
    }
    return S_db_badChoice;
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

rset svectorinRSET={
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

} // namespace

extern "C" {
epicsExportAddress(rset,svectorinRSET);
}
