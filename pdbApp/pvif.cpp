
#include <dbAccess.h>

#include <pv/pvIntrospect.h> /* for pvdVersion.h */
#include <pv/standardField.h>

#include <dbAccess.h>
#include <dbChannel.h>
#include <dbStaticLib.h>
#include <dbLock.h>
#include <dbEvent.h>

#include <pv/bitSet.h>
#include <pv/pvData.h>

#define epicsExportSharedSymbols
#include "pvif.h"

namespace pvd = epics::pvData;

DBCH::DBCH(dbChannel *ch) :chan(ch)
{
    if(dbChannelOpen(chan)) {
        dbChannelDelete(chan);
        throw std::invalid_argument("Failed to open channel");
    }
    if(!chan)
        throw std::invalid_argument(std::string("Invalid channel ")+dbChannelName(ch));
}

DBCH::DBCH(const std::string& name)
    :chan(dbChannelCreate(name.c_str()))
{
    if(!chan)
        throw std::invalid_argument("Invalid channel");
    if(dbChannelOpen(chan)) {
        dbChannelDelete(chan);
        throw std::invalid_argument("Failed to open channel "+name);
    }
}

DBCH::DBCH(const char *name)
    :chan(dbChannelCreate(name))
{
    if(!chan)
        throw std::invalid_argument("Invalid channel");
    if(dbChannelOpen(chan)) {
        dbChannelDelete(chan);
        throw std::invalid_argument(std::string("Failed to open channel ")+name);
    }
}

DBCH::~DBCH()
{
    if(chan) dbChannelDelete(chan);
}

void DBCH::swap(DBCH& o)
{
    std::swap(chan, o.chan);
}

PVIF::PVIF(dbChannel *ch)
    :chan(ch)
{}

namespace {

struct pvCommon {
    dbChannel *chan;

    pvd::uint32 nsecMask;

    pvd::BitSet maskALWAYS, maskVALUE, maskALARM, maskPROPERTY,
                maskVALUEPut;

    pvd::PVLongPtr sec;
    pvd::PVIntPtr status, severity, nsec, userTag;

    pvd::PVDoublePtr displayLow, displayHigh, controlLow, controlHigh;
    pvd::PVStringPtr egu;

    pvd::PVScalarPtr warnLow, warnHigh, alarmLow, alarmHigh;

    pvd::PVScalarPtr prec;

    pvd::PVStringArrayPtr enumopts;

    pvCommon() :chan(NULL), nsecMask(0) {}
};

struct pvScalar : public pvCommon {
    typedef pvd::PVScalar pvd_type;
    pvd::PVScalarPtr value;
};

struct pvArray : public pvCommon {
    typedef pvd::PVScalarArray pvd_type;
    pvd::PVScalarArrayPtr value;
};

struct metaTIME {
    DBRstatus
    DBRtime

    enum {mask = DBR_STATUS | DBR_TIME};
};

struct metaLONG {
    DBRstatus
    DBRunits
    DBRtime
    DBRgrLong
    DBRctrlLong
    DBRalLong

    // included so that field is present, not actually used
    // TODO: split up putMeta() to avoid this..
    DBRprecision
    DBRenumStrs

    enum {mask = DBR_STATUS | DBR_UNITS | DBR_TIME | DBR_GR_LONG | DBR_CTRL_LONG | DBR_AL_LONG};
};

struct metaDOUBLE {
    DBRstatus
    DBRunits
    DBRprecision
    DBRtime
    DBRgrDouble
    DBRctrlDouble
    DBRalDouble

    // similar junk
    DBRenumStrs

    enum {mask = DBR_STATUS | DBR_UNITS | DBR_PRECISION | DBR_TIME | DBR_GR_DOUBLE | DBR_CTRL_DOUBLE | DBR_AL_DOUBLE};
};

struct metaENUM {
    DBRstatus
    DBRtime
    DBRenumStrs

    // similar junk
    DBRunits
    DBRprecision
    DBRgrLong
    DBRctrlLong
    DBRalLong

    enum {mask = DBR_STATUS | DBR_TIME | DBR_ENUM_STRS};
};

struct metaSTRING {
    DBRstatus
    DBRtime

    // similar junk
    DBRenumStrs
    DBRunits
    DBRprecision
    DBRgrLong
    DBRctrlLong
    DBRalLong

    enum {mask = DBR_STATUS | DBR_TIME};
};

// lookup fields and populate pvCommon.  Non-existant fields will be NULL.
void attachMeta(pvCommon& pvm, const pvd::PVStructurePtr& pv)
{
#define FMAP(MNAME, PVT, FNAME, DBE) pvm.MNAME = pv->getSubField<pvd::PVT>(FNAME); \
        if(pvm.MNAME) pvm.mask ## DBE.set(pvm.MNAME->getFieldOffset())
    FMAP(status, PVInt, "alarm.status", ALARM);
    FMAP(severity, PVInt, "alarm.severity", ALARM);
    FMAP(sec, PVLong, "timeStamp.secondsPastEpoch", ALWAYS);
    FMAP(nsec, PVInt, "timeStamp.nanoseconds", ALWAYS);
    FMAP(displayHigh, PVDouble, "display.limitHigh", PROPERTY);
    FMAP(displayLow, PVDouble, "display.limitLow", PROPERTY);
    FMAP(controlHigh, PVDouble, "control.limitHigh", PROPERTY);
    FMAP(controlLow, PVDouble, "control.limitLow", PROPERTY);
    FMAP(egu, PVString, "display.units", PROPERTY);
    //FMAP(prec,  PVScalar, "display.format", PROPERTY);
    FMAP(warnHigh, PVScalar, "alarm.highWarningLimit", PROPERTY);
    FMAP(warnLow,  PVScalar, "alarm.lowWarningLimit", PROPERTY);
    FMAP(alarmHigh, PVScalar, "alarm.highAlarmLimit", PROPERTY);
    FMAP(alarmLow,  PVScalar, "alarm.lowAlarmLimit", PROPERTY);
    FMAP(enumopts,  PVStringArray, "value.choices", PROPERTY);
#undef FMAP
    assert(pvm.status && pvm.severity && pvm.sec && pvm.nsec);
}

template<typename PVM>
void attachAll(PVM& pvm, const pvd::PVStructurePtr& pv)
{
    pvm.value = pv->getSubField<typename PVM::pvd_type>("value.index");
    if(!pvm.value)
        pvm.value = pv->getSubFieldT<typename PVM::pvd_type>("value");
    const pvd::PVField *fld = pvm.value.get();
    pvm.maskVALUE.set(fld->getFieldOffset());
    for(;fld; fld = fld->getParent()) {
        // set field bit and all enclosing structure bits
        pvm.maskVALUEPut.set(fld->getFieldOffset());
    }
    attachMeta(pvm, pv);
}

void putTime(const pvCommon& pv, unsigned dbe, db_field_log *pfl)
{
    metaTIME meta;
    long options = (int)metaTIME::mask, nReq = 0;

    long status = dbChannelGet(pv.chan, dbChannelFinalFieldType(pv.chan), &meta, &options, &nReq, pfl);
    if(status)
        throw std::runtime_error("dbGet for meta fails");

    pvd::int32 nsec = meta.time.nsec;
    if(pv.nsecMask) {
        pv.userTag->put(nsec&pv.nsecMask);
        nsec &= ~pv.nsecMask;
    }
    pv.nsec->put(nsec);    pv.sec->put(meta.time.secPastEpoch+POSIX_TIME_AT_EPICS_EPOCH);
    if(dbe&DBE_ALARM) {
        //pv.status->put(meta.status);
        pv.severity->put(meta.severity);
    }
}

void putValue(dbChannel *chan, pvd::PVScalar* value, db_field_log *pfl)
{
    dbrbuf buf;
    long nReq = 1;

    long status = dbChannelGet(chan, dbChannelFinalFieldType(chan), &buf, NULL, &nReq, pfl);
    if(status)
        throw std::runtime_error("dbGet for meta fails");

    switch(dbChannelFinalFieldType(chan)) {
#define CASE(FTYPE) case DBR_##FTYPE: value->putFrom(buf.dbf_##FTYPE); break
    CASE(CHAR);
    CASE(UCHAR);
    CASE(SHORT);
    CASE(USHORT);
    CASE(ENUM);
    CASE(LONG);
    CASE(ULONG);
    CASE(FLOAT);
    CASE(DOUBLE);
#undef CASE
    case DBR_STRING:
        buf.dbf_STRING[sizeof(buf.dbf_STRING)-1] = '\0';
        value->putFrom<std::string>(buf.dbf_STRING);
        break;
    default:
        throw std::runtime_error("putValue unsupported DBR code");
    }
}

void getValue(dbChannel *chan, pvd::PVScalar* value)
{
    dbrbuf buf;

    switch(dbChannelFinalFieldType(chan)) {
#define CASE(FTYPE, PTYPE) case DBR_##FTYPE: buf.dbf_##FTYPE = value->getAs<PTYPE>(); break
    CASE(CHAR, pvd::int8);
    CASE(UCHAR, pvd::uint8);
    CASE(SHORT, pvd::int16);
    CASE(USHORT, pvd::uint16);
    CASE(LONG, pvd::int32);
    CASE(ULONG, pvd::uint32);
    CASE(FLOAT, float);
    CASE(ENUM, pvd::int16);
    CASE(DOUBLE, double);
#undef CASE
    case DBR_STRING:
    {
        std::string val(value->getAs<std::string>());
        strncpy(buf.dbf_STRING, val.c_str(), sizeof(buf.dbf_STRING));
        buf.dbf_STRING[sizeof(buf.dbf_STRING)-1] = '\0';
    }
        break;
    default:
        throw std::runtime_error("getValue unsupported DBR code");
    }

    long status = dbChannelPut(chan, dbChannelFinalFieldType(chan), &buf, 1);
    if(status)
        throw std::runtime_error("dbPut for meta fails");
}

void getValue(dbChannel *chan, pvd::PVScalarArray* value)
{
    short dbr = dbChannelFinalFieldType(chan);
    pvd::shared_vector<const void> buf;

    assert(dbr!=DBR_STRING);

    value->getAs(buf);
    long nReq = buf.size()/pvd::ScalarTypeFunc::elementSize(value->getScalarArray()->getElementType());

    long status = dbChannelPut(chan, dbr, buf.data(), nReq);
    if(status)
        throw std::runtime_error("dbChannelPut for meta fails");
}

void putValue(dbChannel *chan, pvd::PVScalarArray* value, db_field_log *pfl)
{
    const short dbr = dbChannelFinalFieldType(chan);

    long nReq = dbChannelFinalElements(chan);
    const pvd::ScalarType etype = value->getScalarArray()->getElementType();

    assert(dbr!=DBR_STRING);

    pvd::shared_vector<void> buf(pvd::ScalarTypeFunc::allocArray(etype, nReq)); // TODO: pool?

    long status = dbChannelGet(chan, dbr, buf.data(), NULL, &nReq, pfl);
    if(status)
        throw std::runtime_error("dbChannelGet for meta fails");

    buf.slice(0, nReq*pvd::ScalarTypeFunc::elementSize(etype));

    value->putFrom(pvd::freeze(buf));
}

template<typename META>
void putMeta(const pvCommon& pv, unsigned dbe, db_field_log *pfl)
{
    META meta;
    long options = (int)META::mask, nReq = 0;

    long status = dbChannelGet(pv.chan, dbChannelFinalFieldType(pv.chan), &meta, &options, &nReq, pfl);
    if(status)
        throw std::runtime_error("dbGet for meta fails");

    pvd::int32 nsec = meta.time.nsec;
    if(pv.nsecMask) {
        pv.userTag->put(nsec&pv.nsecMask);
        nsec &= ~pv.nsecMask;
    }
    pv.nsec->put(nsec);
#define FMAP(MNAME, FNAME) pv.MNAME->put(meta.FNAME)
    FMAP(sec, time.secPastEpoch+POSIX_TIME_AT_EPICS_EPOCH);
    if(dbe&DBE_ALARM) {
        //FMAP(status, status);
        FMAP(severity, severity);
    }
    if(dbe&DBE_PROPERTY) {
#undef FMAP
#define FMAP(MASK, MNAME, FNAME) if(META::mask&(MASK) && pv.MNAME) pv.MNAME->put(meta.FNAME)
        FMAP(DBR_GR_DOUBLE|DBR_GR_LONG, displayHigh, upper_disp_limit);
        FMAP(DBR_GR_DOUBLE|DBR_GR_LONG, displayLow, lower_disp_limit);
        FMAP(DBR_CTRL_DOUBLE|DBR_CTRL_DOUBLE, controlHigh, upper_ctrl_limit);
        FMAP(DBR_CTRL_DOUBLE|DBR_CTRL_DOUBLE, controlLow, lower_ctrl_limit);
        FMAP(DBR_GR_DOUBLE|DBR_GR_LONG, egu, units);
#undef FMAP
#define FMAP(MASK, MNAME, FNAME) if(META::mask&(MASK) && pv.MNAME) pv.MNAME->putFrom(meta.FNAME)
        // not handling precision until I get a better idea of what 'format' is supposed to be...
        //FMAP(prec,  PVScalar, "display.format", PROPERTY);
        FMAP(DBR_AL_DOUBLE|DBR_AL_DOUBLE, warnHigh, upper_warning_limit);
        FMAP(DBR_AL_DOUBLE|DBR_AL_DOUBLE, warnLow,  lower_warning_limit);
        FMAP(DBR_AL_DOUBLE|DBR_AL_DOUBLE, alarmHigh, upper_alarm_limit);
        FMAP(DBR_AL_DOUBLE|DBR_AL_DOUBLE, alarmLow,  lower_alarm_limit);
#undef FMAP
        if(pv.enumopts) {
            pvd::shared_vector<std::string> strs(meta.no_str);
            for(size_t i=0; i<strs.size(); i++)
            {
                meta.strs[i][sizeof(meta.strs[i])-1] = '\0';
                strs[i] = meta.strs[i];
            }
            pv.enumopts->replace(pvd::freeze(strs));
        }
    }
}

template<typename PVC, typename META>
void putAll(const PVC &pv, unsigned dbe, db_field_log *pfl)
{
    if(dbe&(DBE_VALUE|DBE_ARCHIVE)) {
        putValue(pv.chan, pv.value.get(), pfl);
    }
    if(!(dbe&DBE_PROPERTY)) {
        putTime(pv, dbe, pfl);
    } else {
        putMeta<META>(pv, dbe, pfl);
    }
}

template<class PVM, typename META>
struct PVIFScalarNumeric : public PVIF
{
    PVM pvmeta;
    const epics::pvData::PVStructurePtr pvalue;

    PVIFScalarNumeric(dbChannel *ch, const epics::pvData::PVFieldPtr& p)
        :PVIF(ch)
        ,pvalue(std::tr1::dynamic_pointer_cast<pvd::PVStructure>(p))
    {
        if(!pvalue)
            throw std::runtime_error("Must attach to structure");
        pdbRecordIterator info(chan);

        pvmeta.chan = ch;
        attachAll(pvmeta, pvalue);
        const char *UT = info.info("pdbUserTag");
        if(UT && strncmp(UT, "nslsb", 5)==0) {
            try{
                pvmeta.nsecMask = epics::pvData::castUnsafe<unsigned>(std::string(&UT[5]));
            }catch(std::exception& e){
                std::cerr<<dbChannelRecord(ch)->name<<" : pdbUserTag nslsb requires a number not '"<<UT[5]<<"'\n";
            }
        }
        if(pvmeta.nsecMask>0 && pvmeta.nsecMask<=32) {
            pvmeta.userTag = pvalue->getSubField<pvd::PVInt>("timeStamp.userTag");
            if(!pvmeta.userTag) {
                pvmeta.nsecMask = 0; // struct doesn't have userTag
            } else {
                pvd::uint64 mask = (1<<pvmeta.nsecMask)-1;
                pvmeta.nsecMask = mask;
                pvmeta.maskALWAYS.set(pvmeta.userTag->getFieldOffset());
            }
        } else
            pvmeta.nsecMask = 0;
    }
    virtual ~PVIFScalarNumeric() {}

    virtual void put(epics::pvData::BitSet& mask, unsigned dbe, db_field_log *pfl) OVERRIDE FINAL
    {
        try{
            putAll<PVM, META>(pvmeta, dbe, pfl);
            mask |= pvmeta.maskALWAYS;
            if(dbe&(DBE_VALUE|DBE_ARCHIVE))
                mask |= pvmeta.maskVALUE;
            if(dbe&DBE_ALARM)
                mask |= pvmeta.maskALARM;
            if(dbe&DBE_PROPERTY)
                mask |= pvmeta.maskPROPERTY;
        }catch(...){
            pvmeta.severity->put(3);
            mask |= pvmeta.maskALARM;
            throw;
        }
    }

    virtual void get(const epics::pvData::BitSet& mask) OVERRIDE FINAL
    {
        if(mask.logical_and(pvmeta.maskVALUEPut))
            getValue(pvmeta.chan, pvmeta.value.get());
    }

    virtual unsigned dbe(const epics::pvData::BitSet& mask) OVERRIDE FINAL
    {
        unsigned ret = 0;
        if(mask.logical_and(pvmeta.maskVALUE))
            ret |= DBE_VALUE;
        if(mask.logical_and(pvmeta.maskALARM))
            ret |= DBE_ALARM;
        if(mask.logical_and(pvmeta.maskPROPERTY))
            ret |= DBE_PROPERTY;
        return ret;
    }
};

} // namespace

pvd::ScalarType DBR2PVD(short dbr)
{
    switch(dbr) {
#define CASE(BASETYPE, PVATYPE, DBFTYPE, PVACODE) case DBR_##DBFTYPE: return pvd::pv##PVACODE;
#define CASE_ENUM
#define CASE_SKIP_BOOL
#include "pvatypemap.h"
#undef CASE_ENUM
#undef CASE_SKIP_BOOL
#undef CASE
    case DBF_STRING: return pvd::pvString;
    default:
        throw std::invalid_argument("Unsupported DBR code");
    }
}

short PVD2DBR(pvd::ScalarType pvt)
{
    switch(pvt) {
#define CASE(BASETYPE, PVATYPE, DBFTYPE, PVACODE) case pvd::pv##PVACODE: return DBR_##DBFTYPE;
#define CASE_SQUEEZE_INT64
#include "pvatypemap.h"
#undef CASE_SQUEEZE_INT64
#undef CASE
    default:
        throw std::invalid_argument("Unsupported pvType code");
    }
}

epics::pvData::FieldConstPtr
ScalarBuilder::dtype(dbChannel *channel)
{
    const short dbr = dbChannelFinalFieldType(channel);
    const long maxelem = dbChannelFinalElements(channel);
    const pvd::ScalarType pvt = DBR2PVD(dbr);

    if(INVALID_DB_REQ(dbr))
        throw std::invalid_argument("DBF code out of range");
    if(maxelem!=1 && dbr==DBR_STRING)
        throw std::invalid_argument("String array not supported");
    if(maxelem!=1 && dbr==DBR_ENUM)
        throw std::invalid_argument("enum array not supported");

    if(dbr==DBR_ENUM)
        return pvd::getStandardField()->enumerated("alarm,timeStamp");

    //TODO: ,valueAlarm for numeric
    std::string options("alarm,timeStamp,display,control");

    if(maxelem==1)
        return pvd::getStandardField()->scalar(pvt, options);
    else
        return pvd::getStandardField()->scalarArray(pvt, options);
}

PVIF*
ScalarBuilder::attach(dbChannel *channel, const epics::pvData::PVFieldPtr& root)
{
    const short dbr = dbChannelFinalFieldType(channel);
    const long maxelem = dbChannelFinalElements(channel);
    //const pvd::ScalarType pvt = DBR2PVD(dbr);

    if(maxelem==1) {
        switch(dbr) {
        case DBR_CHAR:
        case DBR_UCHAR:
        case DBR_SHORT:
        case DBR_USHORT:
        case DBR_LONG:
        case DBR_ULONG:
            return new PVIFScalarNumeric<pvScalar, metaLONG>(channel, root);
        case DBR_FLOAT:
        case DBR_DOUBLE:
            return new PVIFScalarNumeric<pvScalar, metaDOUBLE>(channel, root);
        case DBR_ENUM:
            return new PVIFScalarNumeric<pvScalar, metaENUM>(channel, root);
        case DBR_STRING:
            return new PVIFScalarNumeric<pvScalar, metaSTRING>(channel, root);
        }
    } else {
        switch(dbr) {
        case DBR_CHAR:
        case DBR_UCHAR:
        case DBR_SHORT:
        case DBR_USHORT:
        case DBR_LONG:
        case DBR_ULONG:
            return new PVIFScalarNumeric<pvArray, metaLONG>(channel, root);
        case DBR_FLOAT:
        case DBR_DOUBLE:
            return new PVIFScalarNumeric<pvArray, metaDOUBLE>(channel, root);
        }
    }

    throw std::invalid_argument("Channel has invalid/unsupported DBR type");
}

namespace {
template<class PVD>
struct PVIFPlain : public PVIF
{
    typename PVD::shared_pointer field;
    dbChannel *channel;

    PVIFPlain(dbChannel *channel, const epics::pvData::PVFieldPtr& fld)
        :PVIF(channel)
        ,field(std::tr1::static_pointer_cast<PVD>(fld))
        ,channel(channel)
    {}

    virtual ~PVIFPlain() {}

    virtual void put(epics::pvData::BitSet& mask, unsigned dbe, db_field_log *pfl)
    {
        if(dbe&DBE_VALUE) {
            putValue(channel, field.get(), pfl);
            mask.set(field->getFieldOffset());
        }
    }

    virtual void get(const epics::pvData::BitSet& mask)
    {
        if(mask.get(field->getFieldOffset()))
            getValue(channel, field.get());
    }

    virtual unsigned dbe(const epics::pvData::BitSet& mask)
    {
        if(mask.get(field->getFieldOffset()))
            return DBE_VALUE;
        return 0;
    }
};

struct PlainBuilder : public PVIFBuilder
{
    PlainBuilder() {}
    virtual ~PlainBuilder() {}

    // fetch the structure description
    virtual epics::pvData::FieldConstPtr dtype(dbChannel *channel) OVERRIDE FINAL {
        const short dbr = dbChannelFinalFieldType(channel);
        const long maxelem = dbChannelFinalElements(channel);
        const pvd::ScalarType pvt = DBR2PVD(dbr);

        if(INVALID_DB_REQ(dbr))
            throw std::invalid_argument("DBF code out of range");

        if(maxelem==1)
            return pvd::getFieldCreate()->createScalar(pvt);
        else
            return pvd::getFieldCreate()->createScalarArray(pvt);
    }

    // Attach to a structure instance.
    // must be of the type returned by dtype().
    // need not be the root structure
    virtual PVIF* attach(dbChannel *channel, const epics::pvData::PVFieldPtr& root) OVERRIDE FINAL
    {
        const long maxelem = dbChannelFinalElements(channel);

        if(maxelem==1)
            return new PVIFPlain<pvd::PVScalar>(channel, root);
        else
            return new PVIFPlain<pvd::PVScalarArray>(channel, root);
    }
};
}//namespace

PVIFBuilder::PVIFBuilder()
{}

PVIFBuilder::~PVIFBuilder() {}


PVIFBuilder* PVIFBuilder::create(const std::string& type)
{
    if(type.empty() || type=="scalar")
        return new ScalarBuilder;
    else if(type=="plain")
        return new PlainBuilder;
    else
        throw std::runtime_error("Unknown mapping @type");
}
