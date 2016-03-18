
#include <dbAccess.h>

#include <pv/standardField.h>

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

PVIF::PVIF(dbChannel *ch, const epics::pvData::PVStructurePtr &p)
    :chan(ch)
    ,pvalue(p)
{}

namespace {

struct pvCommon {
    dbChannel *chan;
    short dbr; // actual requested DBR_*

    pvd::BitSet maskALWAYS, maskVALUE, maskALARM, maskPROPERTY;

    pvd::PVLongPtr sec;
    pvd::PVIntPtr status, severity, nsec;

    pvd::PVDoublePtr displayLow, displayHigh, controlLow, controlHigh;
    pvd::PVStringPtr egu;

    pvd::PVScalarPtr warnLow, warnHigh, alarmLow, alarmHigh;

    pvd::PVScalarPtr prec;

    pvd::PVStringArrayPtr enumopts;
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
    FMAP(prec,  PVScalar, "display.format", PROPERTY);
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
    pvm.maskVALUE.set(pvm.value->getFieldOffset());
    attachMeta(pvm, pv);
}

void putTime(const pvCommon& pv, unsigned dbe, db_field_log *pfl)
{
    metaTIME meta;
    long options = (int)metaTIME::mask, nReq = 0;

    long status = dbChannelGet(pv.chan, pv.dbr, &meta, &options, &nReq, pfl);
    if(status)
        throw std::runtime_error("dbGet for meta fails");

    pv.sec->put(meta.time.secPastEpoch+POSIX_TIME_AT_EPICS_EPOCH);
    pv.nsec->put(meta.time.nsec);
    if(dbe&DBE_ALARM) {
        pv.status->put(meta.status);
        pv.severity->put(meta.severity);
    }
}

union dbrbuf {
        epicsInt8		dbf_CHAR;
        epicsUInt8		dbf_UCHAR;
        epicsInt16		dbf_SHORT;
        epicsUInt16		dbf_USHORT;
        epicsEnum16		dbf_ENUM;
        epicsInt32		dbf_LONG;
        epicsUInt32		dbf_ULONG;
        epicsFloat32	dbf_FLOAT;
        epicsFloat64    dbf_DOUBLE;
        char		dbf_STRING[MAX_STRING_SIZE];
};

void putValue(const pvScalar& pv, unsigned dbe, db_field_log *pfl)
{
    dbrbuf buf;
    long nReq = 1;

    long status = dbChannelGet(pv.chan, pv.dbr, &buf, NULL, &nReq, pfl);
    if(status)
        throw std::runtime_error("dbGet for meta fails");

    switch(pv.dbr) {
#define CASE(FTYPE) case DBR_##FTYPE: pv.value->putFrom(buf.dbf_##FTYPE); break
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
        pv.value->putFrom<std::string>(buf.dbf_STRING);
        break;
    default:
        throw std::runtime_error("putValue unsupported DBR code");
    }
}

void getValue(const pvScalar& pv)
{
    dbrbuf buf;

    switch(pv.dbr) {
#define CASE(FTYPE, PTYPE) case DBR_##FTYPE: buf.dbf_##FTYPE = pv.value->getAs<PTYPE>(); break
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
        std::string val(pv.value->getAs<std::string>());
        strncpy(buf.dbf_STRING, val.c_str(), sizeof(buf.dbf_STRING));
        buf.dbf_STRING[sizeof(buf.dbf_STRING)-1] = '\0';
    }
    default:
        throw std::runtime_error("getValue unsupported DBR code");
    }

    long status = dbChannelPut(pv.chan, pv.dbr, &buf, 1);
    if(status)
        throw std::runtime_error("dbPut for meta fails");
}

void getValue(const pvArray& pv)
{
    short dbr = pv.dbr;
    pvd::shared_vector<const void> buf;

    assert(dbr!=DBR_STRING);

    pv.value->getAs(buf);
    long nReq = buf.size()/pvd::ScalarTypeFunc::elementSize(pv.value->getScalarArray()->getElementType());

    long status = dbChannelPut(pv.chan, dbr, buf.data(), nReq);
    if(status)
        throw std::runtime_error("dbChannelPut for meta fails");
}

void putValue(const pvArray& pv, unsigned dbe, db_field_log *pfl)
{
    short dbr = pv.dbr;
    long nReq = dbChannelFinalElements(pv.chan);

    assert(dbr!=DBR_STRING);

    pvd::shared_vector<void> buf(pvd::ScalarTypeFunc::allocArray(pv.value->getScalarArray()->getElementType(), nReq)); // TODO: pool?

    long status = dbChannelGet(pv.chan, dbr, buf.data(), NULL, &nReq, pfl);
    if(status)
        throw std::runtime_error("dbChannelGet for meta fails");

    buf.slice(0, nReq);

    pv.value->putFrom(pvd::freeze(buf));
}

template<typename META>
void putMeta(const pvCommon& pv, unsigned dbe, db_field_log *pfl)
{
    META meta;
    long options = (int)META::mask, nReq = 0;

    long status = dbChannelGet(pv.chan, pv.dbr, &meta, &options, &nReq, pfl);
    if(status)
        throw std::runtime_error("dbGet for meta fails");

#define FMAP(MNAME, FNAME) pv.MNAME->put(meta.FNAME)
    FMAP(sec, time.secPastEpoch+POSIX_TIME_AT_EPICS_EPOCH);
    FMAP(nsec, time.nsec);
    if(dbe&DBE_ALARM) {
        FMAP(status, status);
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
        FMAP(DBR_GR_DOUBLE, prec, precision.dp);
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
        putValue(pv, dbe, pfl);
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

    PVIFScalarNumeric(dbChannel *ch, short dbr, const epics::pvData::PVStructurePtr& p) : PVIF(ch, p)
    {
        pvmeta.chan = ch;
        pvmeta.dbr = dbr;
        attachAll(pvmeta, p);
    }
    virtual ~PVIFScalarNumeric() {}

    virtual void put(epics::pvData::BitSet& mask, unsigned dbe, db_field_log *pfl)
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

    virtual void get(epics::pvData::BitSet& mask)
    {
        if(mask.bitwise_and(pvmeta.maskVALUE))
            getValue(pvmeta);
    }

};

static pvd::ScalarType DBR2PVD(short dbr)
{
    switch(dbr) {
#define MAP(DTYPE, PTYPE) case DBR_##DTYPE: return pvd::pv##PTYPE
    MAP(CHAR, Byte);
    MAP(UCHAR, UByte);
    MAP(SHORT, Short);
    MAP(USHORT, UShort);
    MAP(ENUM, Int); // yes really, Base uses SHORT (16-bit) while PVD uses Int (32-bit)
    MAP(LONG, Int);
    MAP(ULONG, UInt);
    MAP(FLOAT, Float);
    MAP(DOUBLE, Double);
    MAP(STRING, String);
#undef MAP
    default:
        throw std::invalid_argument("Unsupported DBR code");
    }
}

} // namespace

void PVIF::Init()
{
}

pvd::StructureConstPtr PVIF::dtype(dbChannel* chan)
{
    const short dbr = dbChannelFinalFieldType(chan);
    const long maxelem = dbChannelFinalElements(chan);
    const pvd::ScalarType pvt = DBR2PVD(dbr);

    if(INVALID_DB_REQ(dbr))
        throw std::invalid_argument("DBF code out of range");
    if(maxelem!=1 && dbr==DBR_STRING)
        throw std::invalid_argument("String array not supported");
    if(maxelem!=1 && dbr==DBR_ENUM)
        throw std::invalid_argument("enum array not supported");

    if(dbr==DBR_ENUM)
        return pvd::getStandardField()->enumerated("alarm,timeStamp");

    std::string options("alarm,timeStamp,display,control");


    if(maxelem==1)
        return pvd::getStandardField()->scalar(pvt, options);
    else
        return pvd::getStandardField()->scalarArray(pvt, options);
}

PVIF* PVIF::attach(dbChannel* chan, const epics::pvData::PVStructurePtr& root)
{
    const short dbr = dbChannelFinalFieldType(chan);
    const long maxelem = dbChannelFinalElements(chan);
    //const pvd::ScalarType pvt = DBR2PVD(dbr);

    if(maxelem==1) {
        switch(dbr) {
        case DBR_CHAR:
        case DBR_UCHAR:
        case DBR_SHORT:
        case DBR_USHORT:
        case DBR_LONG:
        case DBR_ULONG:
            return new PVIFScalarNumeric<pvScalar, metaLONG>(chan, dbr, root);
        case DBR_FLOAT:
        case DBR_DOUBLE:
            return new PVIFScalarNumeric<pvScalar, metaDOUBLE>(chan, dbr, root);
        case DBR_ENUM:
            return new PVIFScalarNumeric<pvScalar, metaENUM>(chan, dbr, root);
        }
    } else {
        switch(dbr) {
        case DBR_CHAR:
        case DBR_UCHAR:
        case DBR_SHORT:
        case DBR_USHORT:
        case DBR_LONG:
        case DBR_ULONG:
            return new PVIFScalarNumeric<pvArray, metaLONG>(chan, dbr, root);
        case DBR_FLOAT:
        case DBR_DOUBLE:
            return new PVIFScalarNumeric<pvArray, metaDOUBLE>(chan, dbr, root);
        }
    }

    throw std::invalid_argument("Channel has invalid/unsupported DBR type");
}
