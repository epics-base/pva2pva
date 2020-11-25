/*************************************************************************\
* Copyright (c) 2020 Michael Davidsaver
* EPICS BASE is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

#include <list>
#include <map>

#include <recGbl.h>
#include <alarm.h>
#include <errlog.h>

#include <pv/standardField.h>

#define GEN_SIZE_OFFSET
#include "multiArrayCommon.h"
#undef  GEN_SIZE_OFFSET
#include <epicsExport.h>

namespace multiArray {

namespace pvd = epics::pvData;

// used during rset init_record to store column definitions and initial values
struct EntryStorage {
    std::string name, label;
    pvd::ScalarType type;
    pvd::shared_vector<const void> value;
};

struct RPvt {
    bool finished;
    typedef std::list<EntryStorage> entries_t;
    entries_t entries;
    typedef std::map<std::string, EntryStorage*> entries_map_t;
    entries_map_t entries_map;

    RPvt() :finished(false) {}
};

static
void storeAlarm(pvd::PVStructure& root, pvd::BitSet& vld,
               unsigned short sevr, const char* amsg)
{
    pvd::PVScalarPtr fld(root.getSubFieldT<pvd::PVScalar>("alarm.severity"));
    fld->putFrom<pvd::uint16>(sevr);
    vld.set(fld->getFieldOffset());

    //TODO: map status properly
    fld = root.getSubFieldT<pvd::PVScalar>("alarm.status");
    fld->putFrom<pvd::uint16>(sevr ? 1 : 0);
    vld.set(fld->getFieldOffset());

    fld = root.getSubFieldT<pvd::PVScalar>("alarm.message");
    fld->putFrom(std::string(amsg));
    vld.set(fld->getFieldOffset());
}

static
void storeTime(pvd::PVStructure& root, pvd::BitSet& vld,
               const epicsTimeStamp& time, epicsInt32 utag)
{
    pvd::PVScalarPtr fld(root.getSubFieldT<pvd::PVScalar>("timeStamp.secondsPastEpoch"));
    fld->putFrom<pvd::uint32>(time.secPastEpoch + POSIX_TIME_AT_EPICS_EPOCH);
    vld.set(fld->getFieldOffset());

    fld = root.getSubFieldT<pvd::PVScalar>("timeStamp.nanoseconds");
    fld->putFrom<pvd::uint32>(time.nsec);
    vld.set(fld->getFieldOffset());

    fld = root.getSubFieldT<pvd::PVScalar>("timeStamp.userTag");
    fld->putFrom<pvd::int32>(utag);
    vld.set(fld->getFieldOffset());
}

void add_column(void *pvoid, const char* fname, const char* label, pvd::ScalarType type)
{
    multiArrayCommonRecord *prec = (multiArrayCommonRecord*)pvoid;

    if(prec->rpvt->finished)
        throw std::logic_error("can't add_column() after init_record()");

    if(prec->rpvt->entries_map.find(fname)!=prec->rpvt->entries_map.end())
        throw std::logic_error("Can't add_column() with duplicate name");

    prec->rpvt->entries.push_back(EntryStorage());
    EntryStorage& ent = prec->rpvt->entries.back();
    prec->rpvt->entries_map[fname] = &ent;

    ent.name = fname;
    ent.label = label ? label : fname;
    ent.type = type;
}

void add_columns(void *pvoid, const Entry* cols)
{
    multiArrayCommonRecord *prec = (multiArrayCommonRecord*)pvoid;

    for(;cols->name; ++cols) {
        add_column(prec, cols->name, cols->label, cols->type);
    }
}

void set_column(void *pvoid, const char* fname, const pvd::shared_vector<const void>& cdata, const ColMeta* pmeta)
{
    multiArrayCommonRecord *prec = (multiArrayCommonRecord*)pvoid;

    RPvt::entries_map_t::iterator it(prec->rpvt->entries_map.find(fname));
    if(it==prec->rpvt->entries_map.end())
        throw std::logic_error("No such column");

    EntryStorage& ent = *it->second;

    if(!prec->rpvt->finished) {
        // save for later during init_record(, 1)
        ent.value = cdata;
        // TODO: save meta

    } else {
        // called from process()

        if(prec->lay==menuMultiArrayLayoutTable) {
            pvd::PVScalarArrayPtr arr(prec->val->getSubFieldT<pvd::PVScalarArray>(ent.name));
            arr->putFrom(cdata);
            prec->vld.set(arr->getFieldOffset());

        } else if(prec->lay==menuMultiArrayLayoutComposite) {
            pvd::PVStructurePtr base(prec->val->getSubFieldT<pvd::PVStructure>(ent.name));

            pvd::PVScalarArrayPtr arr(base->getSubFieldT<pvd::PVScalarArray>("value"));
            arr->putFrom(cdata);
            prec->vld.set(arr->getFieldOffset());

            if(pmeta) {
                storeAlarm(*base, prec->vld, pmeta->sevr, pmeta->amsg ? pmeta->amsg : "");
                storeTime(*base, prec->vld, pmeta->time, pmeta->utag);
            }
        }
    }
}

void get_column(void *pvoid, const char* fname, ::epics::pvData::shared_vector<const void>& cdata, ColMeta* pmeta)
{
    multiArrayCommonRecord *prec = (multiArrayCommonRecord*)pvoid;

    RPvt::entries_map_t::iterator it(prec->rpvt->entries_map.find(fname));
    if(it==prec->rpvt->entries_map.end())
        throw std::logic_error("No such column");

    EntryStorage& ent = *it->second;

    if(!prec->rpvt->finished) {
        cdata = ent.value;

    } else {
        if(prec->lay==menuMultiArrayLayoutTable) {
            prec->val->getSubFieldT<pvd::PVScalarArray>(ent.name)->getAs(cdata);

        } else if(prec->lay==menuMultiArrayLayoutComposite) {
            pvd::PVStructurePtr base(prec->val->getSubFieldT<pvd::PVStructure>(ent.name));
            base->getSubFieldT<pvd::PVScalarArray>("value")->getAs(cdata);

            if(pmeta) {
                // TODO: extract alarm/time
            }
        }
    }
}

#define TRY try
#define CATCH() catch(std::exception& e) { \
    (void)recGblSetSevrMsg(prec, COMM_ALARM, INVALID_ALARM, "exc: %s", e.what()); \
    errlogPrintf("%s: unhandled exception: %s\n", prec->name, e.what()); \
}


ELLLIST vfList = ELLLIST_INIT;
VFieldTypeNode vfNodes[2];

long initialize()
{
    vfNodes[0].vtype = &vfStructure;
    ellAdd(&vfList, &vfNodes[0].node);
    vfNodes[1].vtype = &vfPVStructure;
    ellAdd(&vfList, &vfNodes[1].node);
    return 0;
}

long init_record(struct dbCommon *pcommon, int pass)
{
    multiArrayCommonRecord *prec = (multiArrayCommonRecord*)pcommon;
    TRY {
        if(pass==0) {
            prec->rpvt = new RPvt;
            new (&prec->val) pvd::PVStructurePtr();
            new (&prec->vld) pvd::BitSet;

            // caller may now add_column()

            return 0;

        } else if(pass==1) {
            // caller has made all calls to add_column()

            prec->rpvt->finished = true;

            const pvd::StandardFieldPtr& sfld(pvd::getStandardField());

            pvd::FieldBuilderPtr builder = pvd::FieldBuilder::begin();

            if(prec->lay==menuMultiArrayLayoutTable) {

                pvd::shared_vector<std::string> labels;
                labels.reserve(prec->rpvt->entries.size());

                builder = builder->setId("epics:nt/NTTable:1.0")
                        ->addArray("labels", pvd::pvString)
                        ->addNestedStructure("value");

                for(RPvt::entries_t::const_iterator it(prec->rpvt->entries.begin()),
                    end(prec->rpvt->entries.end()); it!=end; ++it)
                {
                    const EntryStorage& ent = *it;

                    builder = builder->addArray(ent.name, ent.type);
                    labels.push_back(ent.label);
                }

                builder = builder->endNested()
                        ->add("alarm", sfld->alarm())
                        ->add("timeStamp", sfld->timeStamp());

                prec->val = builder->createStructure()->build();

                pvd::PVStringArrayPtr flabels(prec->val->getSubFieldT<pvd::PVStringArray>("labels"));
                flabels->replace(pvd::freeze(labels));
                prec->vld.set(flabels->getFieldOffset());

                pvd::PVStructurePtr value(prec->val->getSubFieldT<pvd::PVStructure>("value"));

                for(RPvt::entries_t::const_iterator it(prec->rpvt->entries.begin()),
                    end(prec->rpvt->entries.end()); it!=end; ++it)
                {
                    const EntryStorage& ent = *it;

                    if(!ent.value.empty())
                        continue;

                    pvd::PVScalarArrayPtr fld(value->getSubFieldT<pvd::PVScalarArray>(ent.name));
                    fld->putFrom(ent.value);
                    prec->vld.set(fld->getFieldOffset());
                }

            } else if(prec->lay==menuMultiArrayLayoutComposite) {

                for(RPvt::entries_t::const_iterator it(prec->rpvt->entries.begin()),
                    end(prec->rpvt->entries.end()); it!=end; ++it)
                {
                    const EntryStorage& ent = *it;

                    pvd::StructureConstPtr substruct(sfld->scalarArray(ent.type, "alarm,timeStamp"));

                    builder = builder->add(ent.name, substruct);
                }

                prec->val = builder->createStructure()->build();

                for(RPvt::entries_t::iterator it(prec->rpvt->entries.begin()),
                    end(prec->rpvt->entries.end()); it!=end; ++it)
                {
                    EntryStorage& ent = *it;

                    if(!ent.value.empty())
                        continue;

                    pvd::PVStructurePtr base(prec->val->getSubFieldT<pvd::PVStructure>(ent.name));

                    pvd::PVScalarArrayPtr fld(base->getSubFieldT<pvd::PVScalarArray>("value"));
                    fld->putFrom(ent.value);
                    ent.value = pvd::shared_vector<const void>(); // clear
                    prec->vld.set(fld->getFieldOffset());

                    // TODO initial alarm/time
                }

            } else {
                errlogPrintf("%s.LAY: out of range value %d\n", prec->name, prec->lay);
                (void)recGblSetSevrMsg(prec, COMM_ALARM, INVALID_ALARM, "Bad LAY %d", prec->lay);
                return -1;
            }

            return 0;
        }
    }CATCH()
    return -1;
}

void monitor(void *pvoid, unsigned short monitor_mask)
{
    multiArrayCommonRecord *prec = (multiArrayCommonRecord*)pvoid;

    if(monitor_mask&DBE_ALARM) {
        // sync record alarm into PVD structure
        if(prec->lay==menuMultiArrayLayoutTable) {
            storeAlarm(*prec->val, prec->vld, prec->sevr, prec->amsg);

        } else if(prec->lay==menuMultiArrayLayoutComposite) {
            // no top level alarm
        }
    }

    if(prec->lay==menuMultiArrayLayoutTable) {
        storeTime(*prec->val, prec->vld, prec->time, prec->utag);

    } else if(prec->lay==menuMultiArrayLayoutComposite) {
        // no top level timestamp
    }

    if (monitor_mask) {
        db_post_events(prec, &prec->val, monitor_mask);
    }
}

long cvt_dbaddr(DBADDR *paddr)
{
    dbCommon *prec = paddr->precord;
    TRY {
        // for VAL

        // we don't provide a valid DBR buffer
        paddr->ro = 1;
        // arbitrary limit
        paddr->no_elements = 1;

        paddr->field_type = DBF_NOACCESS;

        if(dbGetFieldIndex(paddr)==multiArrayCommonRecordVAL) {
            // we provide vfield access
            paddr->vfields = &vfList;
        }

        return 0;
    }CATCH()
    return -1;
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
    multiArrayCommonRecord *prec = (multiArrayCommonRecord*)paddr->precord;
    TRY {

        if(!prec->val)
            return S_db_notInit;

        if(p->vtype==&vfPVStructure) {
            VSharedPVStructure *pstr = (VSharedPVStructure*)p;
            if(dbGetFieldIndex(paddr)==multiArrayCommonRecordVAL) {
                if(!*pstr->value)
                    return S_db_notInit;
                (*pstr->value)->copy(*prec->val);
                *pstr->changed = prec->vld;
                return 0;
            }

        } else if(p->vtype==&vfStructure) {
            VSharedStructure *pstr = (VSharedStructure*)p;
            if(dbGetFieldIndex(paddr)==multiArrayCommonRecordVAL) {
                *pstr->value = prec->val->getStructure();
                return 0;
            }
        }
        return S_db_badChoice;
    }CATCH()
    return -1;
}

long put_vfield(struct dbAddr *paddr, const struct VField *p)
{
    multiArrayCommonRecord *prec = (multiArrayCommonRecord*)paddr->precord;
    TRY {
        if(!prec->val)
            return S_db_notInit;

        if(p->vtype==&vfPVStructure) {
            const VSharedPVStructure *pstr = (const VSharedPVStructure*)p;
            if(dbGetFieldIndex(paddr)==multiArrayCommonRecordVAL) {
                prec->val->copy(**pstr->value);
                prec->vld |= *pstr->changed;
            }
        }
        return S_db_badChoice;
    }CATCH()
    return -1;
}

} // namespace multiArray
