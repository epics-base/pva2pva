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

// include by pvstructinRecord.h
#include "epicsTypes.h"
#include "link.h"
#include "epicsMutex.h"
#include "ellLib.h"
#include "devSup.h"
#include "epicsTime.h"

#include <pv/qsrv.h>

#define GEN_SIZE_OFFSET
#include <pvstructinRecord.h>
#undef  GEN_SIZE_OFFSET
#include <epicsExport.h>

namespace  {

namespace pvd = epics::pvData;

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
    pvstructinRecord *prec = (pvstructinRecord*)pcommon;
    pvstructindset *pdset = (pvstructindset *)(prec->dset);

    if (!pdset) {
        if(pass==0)
            recGblRecordError(S_dev_noDSET, prec, "pvstructin: no DSET");
        return S_dev_noDSET;
    }

    if(pass==0) {
        new (&prec->val) pvd::PVStructurePtr();
        new (&prec->chg) pvd::BitSet();
        new (&prec->ptyp) pvd::StructureConstPtr();

    } else { // pass==1

        if(pdset->common.init_record) {
            long ret = pdset->common.init_record(pcommon);
            if(ret)
                return ret;
        }

        if(!prec->val) {
            recGblRecordError(S_dev_noDSET, prec, "pvstructin: init_record must set VAL with a valid PVStructure");
            return S_db_badDbrtype;
        }

        prec->ptyp = prec->val->getStructure();
    }
    return 0;
}

long readValue(pvstructinRecord *prec,
               pvstructindset *pdset)
{
    prec->chg.clear();

    long ret = pdset->read_pvs(prec);

    if(!ret && prec->val && prec->val->getStructure()!=prec->ptyp) {
        recGblSetSevrMsg(prec, READ_ALARM, INVALID_ALARM, "VAL must use PTYP");
        prec->val.reset();
        prec->chg.clear();
        ret = S_db_badDbrtype;
    }

    return  ret;
}

void monitor(pvstructinRecord *prec)
{
    int monitor_mask = recGblResetAlarms(prec);

    if (!prec->chg.isEmpty()) {
        monitor_mask |= DBE_VALUE | DBE_LOG;
    }

    if (monitor_mask) {
        db_post_events(prec, &prec->val, monitor_mask);
    }
}

long process(struct dbCommon *pcommon)
{
    pvstructinRecord *prec = (pvstructinRecord*)pcommon;
    pvstructindset *pdset = (pvstructindset *)(prec->dset);
    unsigned char    pact=prec->pact;
    long status;

    if( (pdset==NULL) || (pdset->read_pvs==NULL) ) {
        prec->pact=TRUE;
        recGblSetSevrMsg(prec, READ_ALARM, INVALID_ALARM, "no read_pvs");
        recGblRecordError(S_dev_missingSup,(void *)prec,"read_pvs");
        return S_dev_missingSup;

    } else if(!prec->ptyp) {
        prec->pact=TRUE;
        recGblSetSevrMsg(prec, READ_ALARM, INVALID_ALARM, "no no PTYP");
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
}

long cvt_dbaddr(DBADDR *paddr)
{
    pvstructinRecord *prec = (pvstructinRecord*)paddr->precord;
    // for both VAL and OVAL

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
    pvstructinRecord *prec = (pvstructinRecord*)paddr->precord;

    if(!prec->ptyp)
        return S_db_notInit;

    if(p->vtype==&vfPVStructure) {
        VSharedPVStructure *pstr = (VSharedPVStructure*)p;
        if(dbGetFieldIndex(paddr)==pvstructinRecordVAL) {
            if(!*pstr->value)
                return S_db_notInit;
            (*pstr->value)->copy(*prec->val);
            *pstr->changed = prec->chg;
            return 0;
        }

    } else if(p->vtype==&vfStructure) {
        VSharedStructure *pstr = (VSharedStructure*)p;
        if(dbGetFieldIndex(paddr)==pvstructinRecordVAL) {
            *pstr->value = prec->ptyp;
            return 0;
        }
    }
    return S_db_badChoice;
}

long put_vfield(struct dbAddr *paddr, const struct VField *p)
{
    pvstructinRecord *prec = (pvstructinRecord*)paddr->precord;

    if(!prec->ptyp)
        return S_db_notInit;

    if(p->vtype==&vfPVStructure) {
        const VSharedPVStructure *pstr = (const VSharedPVStructure*)p;
        if(dbGetFieldIndex(paddr)==pvstructinRecordVAL) {
            prec->val->copy(**pstr->value);
            prec->chg = *pstr->changed;
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

rset pvstructinRSET={
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
epicsExportAddress(rset,pvstructinRSET);
}
