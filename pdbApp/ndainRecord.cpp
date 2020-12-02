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

// include by ndainRecord.h
#include "epicsTypes.h"
#include "link.h"
#include "epicsMutex.h"
#include "ellLib.h"
#include "devSup.h"
#include "epicsTime.h"

#include <pv/pvData.h>
#include <pv/standardField.h>
#include <pv/bitSet.h>
#include <pv/qsrv.h>

#define GEN_SIZE_OFFSET
#include <ndainRecord.h>
#undef  GEN_SIZE_OFFSET
#include <epicsExport.h>

namespace pvd = epics::pvData;

struct ndainPvt
{
};

namespace  {

ELLLIST vfPVStructureList = ELLLIST_INIT;
VFieldTypeNode vfPVStructureNode[2];

pvd::StructureConstPtr ntndarray;

long initialize()
{
    vfPVStructureNode[0].vtype = &vfStructure;
    ellAdd(&vfPVStructureList, &vfPVStructureNode[0].node);
    vfPVStructureNode[1].vtype = &vfPVStructure;
    ellAdd(&vfPVStructureList, &vfPVStructureNode[1].node);

    pvd::FieldConstPtr any = pvd::getFieldCreate()->createVariantUnion();
    pvd::StandardFieldPtr sf = pvd::StandardField::getStandardField();
    pvd::StructureConstPtr timeStamp = sf->timeStamp();

    // avoid pulling in normativeTypeCPP dependency
    ntndarray = pvd::FieldBuilder::begin()
            ->setId("epics:nt/NTNDArray:1.0")
            ->addNestedUnion("value")
                ->addArray("booleanValue", pvd::pvBoolean)
                ->addArray("byteValue", pvd::pvByte)
                ->addArray("shortValue", pvd::pvShort)
                ->addArray("intValue", pvd::pvInt)
                ->addArray("longValue", pvd::pvLong)
                ->addArray("ubyteValue", pvd::pvUByte)
                ->addArray("ushortValue", pvd::pvUShort)
                ->addArray("uintValue", pvd::pvUInt)
                ->addArray("ulongValue", pvd::pvULong)
                ->addArray("floatValue", pvd::pvFloat)
                ->addArray("doubleValue", pvd::pvDouble)
            ->endNested()
            ->addNestedStructure("codec")
                ->setId("codec_t")
                ->add("name", pvd::pvString)
                ->add("parameters", any)
            ->endNested()
            ->add("compressedSize", pvd::pvLong)
            ->add("uncompressedSize", pvd::pvLong)
            ->add("uniqueId", pvd::pvInt)
            ->add("dataTimeStamp", timeStamp)
            ->add("alarm", sf->alarm())
            ->add("timeStamp", timeStamp)
            ->addNestedStructureArray("dimension")
                ->setId("dimension_t")
                ->add("size", pvd::pvInt)
                ->add("offset", pvd::pvInt)
                ->add("fullSize", pvd::pvInt)
                ->add("binning", pvd::pvInt)
                ->add("reverse", pvd::pvBoolean)
            ->endNested()
            ->addNestedStructureArray("attribute")
                ->setId("epics:nt/NTAttribute:1.0")
                ->add("name", pvd::pvString)
                ->add("value", any)
                ->addArray("tags", pvd::pvString)
                ->add("descriptor", pvd::pvString)
                ->add("alarm", sf->alarm())
                ->add("timeStamp", timeStamp)
                ->add("sourceType", pvd::pvInt)
                ->add("source", pvd::pvString)
            ->endNested()
            ->createStructure();

    return 0;
}


long init_record(struct dbCommon *pcommon, int pass)
{
    ndainRecord *prec = (ndainRecord*)pcommon;
    ndaindset *pdset = (ndaindset *)(prec->dset);

    if(pass==0) {
        new (&prec->val) pvd::shared_vector<const void>();
        prec->rpvt = new ndainPvt;
        new (&prec->root) pvd::PVStructurePtr();
        new (&prec->chg) pvd::BitSet();
        new (&prec->vld) pvd::BitSet();
        pvd::PVStructurePtr root = prec->root = ntndarray->build();

        if (!pdset) {
            recGblSetSevrMsg(prec, READ_ALARM, INVALID_ALARM, "no DSET");
            recGblRecordError(S_dev_noDSET, prec, "ndain: no DSET");
            return S_dev_noDSET;
        }

    } else { // pass==1

        if(pdset->common.init_record) {
            long ret = pdset->common.init_record(pcommon);
            if(ret)
                return ret;
        }
    }
    return 0;
}

long readValue(ndainRecord *prec,
               ndaindset *pdset)
{
    epicsUInt32 origShape[2] = {prec->w, prec->h};
    epicsInt32 oldID = prec->id;
    long status = 0;

    if(!status && !dbLinkIsConstant(&prec->inh))
        status=dbGetLink(&prec->inh, DBF_ULONG, &prec->h, 0, 0);

    if(!status && !dbLinkIsConstant(&prec->inw))
        status=dbGetLink(&prec->inw, DBF_ULONG, &prec->w, 0, 0);

    if(status)
        return status;

    prec->chg.clear();

    status = pdset->read_ndarray(prec);

    pvd::PVUnionPtr value = prec->root->getSubFieldT<pvd::PVUnion>("value");

    switch(prec->val.original_type()) {
    case pvd::pvBoolean: value->select<pvd::PVScalarArray>("booleanValue")->putFrom(prec->val); break;
    case pvd::pvByte:    value->select<pvd::PVScalarArray>("byteValue")->putFrom(prec->val); break;
    case pvd::pvShort:   value->select<pvd::PVScalarArray>("shortValue")->putFrom(prec->val); break;
    case pvd::pvInt:     value->select<pvd::PVScalarArray>("intValue")->putFrom(prec->val); break;
    case pvd::pvLong:    value->select<pvd::PVScalarArray>("longValue")->putFrom(prec->val); break;
    case pvd::pvUByte:   value->select<pvd::PVScalarArray>("ubyteValue")->putFrom(prec->val); break;
    case pvd::pvUShort:  value->select<pvd::PVScalarArray>("ushortValue")->putFrom(prec->val); break;
    case pvd::pvUInt:    value->select<pvd::PVScalarArray>("uintValue")->putFrom(prec->val); break;
    case pvd::pvULong:   value->select<pvd::PVScalarArray>("ulongValue")->putFrom(prec->val); break;
    case pvd::pvFloat:   value->select<pvd::PVScalarArray>("floatValue")->putFrom(prec->val); break;
    case pvd::pvDouble:  value->select<pvd::PVScalarArray>("doubleValue")->putFrom(prec->val); break;
    default:
        recGblSetSevrMsg(prec, READ_ALARM, INVALID_ALARM, "unsup arr type %u", (unsigned)prec->val.original_type());
        // fall through
    case (pvd::ScalarType)-1:
        value->select(pvd::PVUnion::UNDEFINED_INDEX);
        break;
    }
    prec->chg.set(value->getFieldOffset());

    pvd::PVStructureArrayPtr dims = prec->root->getSubFieldT<pvd::PVStructureArray>("dimension");
    {
        if(origShape[0]!=prec->w)
            db_post_events(prec, &prec->w, DBE_VALUE|DBE_ARCHIVE);
        if(origShape[1]!=prec->h)
            db_post_events(prec, &prec->h, DBE_VALUE|DBE_ARCHIVE);

        pvd::StructureConstPtr elemType = dims->getStructureArray()->getStructure();
        pvd::PVStructureArray::svector arr(2);
        arr[0] = elemType->build();
        arr[1] = elemType->build();

        arr[0]->getSubFieldT<pvd::PVInt>("size")->put(prec->w);
        arr[1]->getSubFieldT<pvd::PVInt>("size")->put(prec->h);

        dims->replace(pvd::freeze(arr));
    }
    prec->chg.set(dims->getFieldOffset());

    if(oldID!=prec->id) {
        pvd::PVIntPtr id(prec->root->getSubFieldT<pvd::PVInt>("uniqueId"));
        id->put(prec->id);
        prec->chg.set(id->getFieldOffset());
    }

    prec->vld |= prec->chg;

    return status;
}

void monitor(ndainRecord *prec)
{
    int monitor_mask = recGblResetAlarms(prec);

    if(monitor_mask&DBE_ALARM) {
        pvd::PVScalarPtr fld(prec->root->getSubFieldT<pvd::PVScalar>("alarm.severity"));
        fld->putFrom<pvd::uint16>(prec->sevr);
        prec->chg.set(fld->getFieldOffset());

        //TODO: map status properly
        fld = prec->root->getSubFieldT<pvd::PVScalar>("alarm.status");
        fld->putFrom<pvd::uint16>(prec->stat ? 1 : 0);
        prec->chg.set(fld->getFieldOffset());

        fld = prec->root->getSubFieldT<pvd::PVScalar>("alarm.message");
        fld->putFrom(std::string(prec->amsg));
        prec->chg.set(fld->getFieldOffset());
    }

    {
        pvd::PVScalarPtr fld(prec->root->getSubFieldT<pvd::PVScalar>("timeStamp.secondsPastEpoch"));
        fld->putFrom<pvd::uint32>(prec->time.secPastEpoch + POSIX_TIME_AT_EPICS_EPOCH);
        prec->chg.set(fld->getFieldOffset());

        fld = prec->root->getSubFieldT<pvd::PVScalar>("timeStamp.nanoseconds");
        fld->putFrom<pvd::uint32>(prec->time.nsec);
        prec->chg.set(fld->getFieldOffset());

        fld = prec->root->getSubFieldT<pvd::PVScalar>("timeStamp.userTag");
        fld->putFrom<pvd::uint64>(prec->utag);
        prec->chg.set(fld->getFieldOffset());

        monitor_mask |= DBE_VALUE|DBE_ARCHIVE;
    }

    db_post_events(prec, &prec->val, monitor_mask);
}

long process(struct dbCommon *pcommon)
{
    ndainRecord *prec = (ndainRecord*)pcommon;
    ndaindset *pdset = (ndaindset *)(prec->dset);
    unsigned char    pact=prec->pact;
    long status;

    if( (pdset==NULL) || (pdset->read_ndarray==NULL) ) {
        prec->pact=TRUE;
        recGblSetSevrMsg(prec, READ_ALARM, INVALID_ALARM, "no read_ndain");
        recGblRecordError(S_dev_missingSup,(void *)prec,"read_ndain");
        return S_dev_missingSup;
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
    ndainRecord *prec = (ndainRecord*)paddr->precord;
    // for both VAL and OVAL

    // we don't insist devsup allocate the required NELM
    paddr->ro = 1;
    // we provide vfield access
    paddr->vfields = &vfPVStructureList;
    // arbitrary limit
    paddr->no_elements = 1;

    paddr->field_type = DBF_NOACCESS;

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
    ndainRecord *prec = (ndainRecord*)paddr->precord;

    if(p->vtype==&vfPVStructure) {
        VSharedPVStructure *pstr = (VSharedPVStructure*)p;
        if(dbGetFieldIndex(paddr)==ndainRecordVAL) {
            if(!*pstr->value)
                return S_db_notInit;
            (*pstr->value)->copy(*prec->root);
            *pstr->changed = prec->vld;
            return 0;
        }

    } else if(p->vtype==&vfStructure) {
        VSharedStructure *pstr = (VSharedStructure*)p;
        if(dbGetFieldIndex(paddr)==ndainRecordVAL) {
            *pstr->value = ntndarray;
            return 0;
        }
    }
    return S_db_badChoice;
}

long put_vfield(struct dbAddr *paddr, const struct VField *p)
{
    ndainRecord *prec = (ndainRecord*)paddr->precord;

    if(p->vtype==&vfPVStructure) {
        const VSharedPVStructure *pstr = (const VSharedPVStructure*)p;
        if(dbGetFieldIndex(paddr)==ndainRecordVAL) {
            prec->root->copy(**pstr->value);
            prec->vld |= *pstr->changed;
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

rset ndainRSET={
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
epicsExportAddress(rset,ndainRSET);
}
