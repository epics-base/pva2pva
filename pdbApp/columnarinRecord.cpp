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

#include <pvstructinRecord.h>

#define GEN_SIZE_OFFSET
#include <columnarinRecord.h>
#undef  GEN_SIZE_OFFSET
#include <epicsExport.h>

namespace pvd = epics::pvData;

namespace columnarin {

struct RPvt {
    pvd::shared_vector<std::string> labels;
    pvd::FieldBuilderPtr builder;
    pvd::PVStructurePtr tbl;
    pvd::BitSet valid, changed;
};

void add_column(struct columnarinRecord *prec, const char* fname, const char* label, ::epics::pvData::ScalarType type)
{
    if(!prec->rpvt->builder)
        throw std::logic_error("Only from init_record()");

    prec->rpvt->builder = prec->rpvt->builder->addArray(fname, type);
    prec->rpvt->labels.push_back(label ? label : fname);
}

void set_column(struct columnarinRecord *prec, const char* fname, const ::epics::pvData::shared_vector<const void>& cdata)
{
    if(!prec->rpvt->tbl)
        throw std::logic_error("Only from read_tbl()");

    pvd::PVScalarArrayPtr col(prec->rpvt->tbl->getSubFieldT<pvd::PVStructure>("value")->getSubFieldT<pvd::PVScalarArray>(fname));
    col->putFrom(cdata);
    prec->rpvt->changed.set(col->getFieldOffset());

}

} // namespace columnarin

namespace {
using namespace columnarin;

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
    columnarinRecord *prec = (columnarinRecord*)pcommon;
    columnarindset *pdset = (columnarindset *)(prec->dset);
    try {

        if (!pdset) {
            if(pass==0)
                recGblRecordError(S_dev_noDSET, prec, "pvstructin: no DSET");
            return S_dev_noDSET;
        }

        if(pass==0) {
            prec->rpvt = new RPvt;

        } else { // pass==1
            prec->rpvt->builder = pvd::FieldBuilder::begin()
                    ->setId("epics:nt/NTTable:1.0")
                    ->addArray("labels", pvd::pvString)
                    ->addNestedStructure("value");

            if(pdset->common.init_record) {
                long ret = pdset->common.init_record(pcommon);
                if(ret)
                    return ret;
            }

            const pvd::StandardFieldPtr& sfld(pvd::getStandardField());

            prec->rpvt->tbl = prec->rpvt->builder->endNested()
                                     ->add("alarm", sfld->alarm())
                                     ->add("timeStamp", sfld->timeStamp())
                                     ->createStructure()->build();

            pvd::PVStringArrayPtr lbls(prec->rpvt->tbl->getSubFieldT<pvd::PVStringArray>("labels"));
            lbls->replace(pvd::freeze(prec->rpvt->labels));
            prec->rpvt->valid.set(lbls->getFieldOffset());
        }

        return 0;
    }catch(std::exception& e){
        fprintf(stderr, "%s: init_record error: %s\n", prec->name, e.what());
        return -1;
    }
}

long readValue(columnarinRecord *prec,
               columnarindset *pdset)
{
    prec->rpvt->changed.clear();

    long ret = pdset->read_tbl(prec);

    return  ret;
}

void monitor(columnarinRecord *prec)
{
    int monitor_mask = recGblResetAlarms(prec);

    {
        // was set_column called during read_tbl()?
        pvd::int32 bit = prec->rpvt->changed.nextSetBit(0u);
        if(bit>=0 && pvd::uint32(bit)<prec->rpvt->tbl->getNextFieldOffset())
            monitor_mask |= DBE_VALUE|DBE_LOG;
    }

    if(monitor_mask&DBE_ALARM) {
        pvd::PVScalarPtr fld(prec->rpvt->tbl->getSubFieldT<pvd::PVScalar>("alarm.severity"));
        fld->putFrom<pvd::uint16>(prec->sevr);
        prec->rpvt->changed.set(fld->getFieldOffset());

        //TODO: map status properly
        fld = prec->rpvt->tbl->getSubFieldT<pvd::PVScalar>("alarm.status");
        fld->putFrom<pvd::uint16>(prec->stat ? 1 : 0);
        prec->rpvt->changed.set(fld->getFieldOffset());

        fld = prec->rpvt->tbl->getSubFieldT<pvd::PVScalar>("alarm.message");
        fld->putFrom(std::string(prec->amsg));
        prec->rpvt->changed.set(fld->getFieldOffset());
    }

    {
        pvd::PVScalarPtr fld(prec->rpvt->tbl->getSubFieldT<pvd::PVScalar>("timeStamp.secondsPastEpoch"));
        fld->putFrom<pvd::uint32>(prec->time.secPastEpoch + POSIX_TIME_AT_EPICS_EPOCH);
        prec->rpvt->changed.set(fld->getFieldOffset());

        fld = prec->rpvt->tbl->getSubFieldT<pvd::PVScalar>("timeStamp.nanoseconds");
        fld->putFrom<pvd::uint32>(prec->time.nsec);
        prec->rpvt->changed.set(fld->getFieldOffset());

        fld = prec->rpvt->tbl->getSubFieldT<pvd::PVScalar>("timeStamp.userTag");
        fld->putFrom<pvd::int32>(prec->utag);
        prec->rpvt->changed.set(fld->getFieldOffset());
    }

    monitor_mask |= DBE_VALUE | DBE_LOG;

    if (monitor_mask) {
        db_post_events(prec, &prec->val, monitor_mask);
    }

    prec->rpvt->valid |= prec->rpvt->changed;
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
            recGblSetSevrMsg(prec, READ_ALARM, INVALID_ALARM, "no read_pvs");
            recGblRecordError(S_dev_missingSup,(void *)prec,"read_pvs");
            return S_dev_missingSup;

        } else if(!prec->rpvt->tbl) {
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
    columnarinRecord *prec = (columnarinRecord*)paddr->precord;

    if(!prec->rpvt->tbl)
        return S_db_notInit;

    if(p->vtype==&vfPVStructure) {
        VSharedPVStructure *pstr = (VSharedPVStructure*)p;
        if(dbGetFieldIndex(paddr)==pvstructinRecordVAL) {
            if(!*pstr->value)
                return S_db_notInit;
            (*pstr->value)->copy(*prec->rpvt->tbl);
            *pstr->changed = prec->rpvt->valid; // TODO: distinguish initial from update
            return 0;
        }

    } else if(p->vtype==&vfStructure) {
        VSharedStructure *pstr = (VSharedStructure*)p;
        if(dbGetFieldIndex(paddr)==pvstructinRecordVAL) {
            *pstr->value = prec->rpvt->tbl->getStructure();
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

long init_record_soft(dbCommon *prec)
{
    return 0;
}

long read_tbl_soft(columnarinRecord* prec)
{
    (void)recGblSetSevr(prec, READ_ALARM, INVALID_ALARM);
    return 0;
}

columnarindset devCOLISoft = {
    {5, NULL, NULL, &init_record_soft, NULL},
    &read_tbl_soft
};


} // namespace

extern "C" {
epicsExportAddress(rset,columnarinRSET);
epicsExportAddress(dset, devCOLISoft);
}
