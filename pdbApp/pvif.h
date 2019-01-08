#ifndef PVIF_H
#define PVIF_H

#include <map>

#include <dbAccess.h>
#include <dbChannel.h>
#include <dbStaticLib.h>
#include <dbLock.h>
#include <dbEvent.h>
#include <epicsVersion.h>

#include <pv/status.h>
#include <pv/bitSet.h>
#include <pv/pvData.h>
#include <pv/anyscalar.h>

#include <shareLib.h>

#ifndef VERSION_INT
#  define VERSION_INT(V,R,M,P) ( ((V)<<24) | ((R)<<16) | ((M)<<8) | (P))
#endif

#ifndef EPICS_VERSION_INT
#  define EPICS_VERSION_INT VERSION_INT(EPICS_VERSION, EPICS_REVISION, EPICS_MODIFICATION, EPICS_PATCH_LEVEL)
#endif

#if EPICS_VERSION_INT>=VERSION_INT(3,16,0,2)
#  define USE_MULTILOCK
#endif

short PVD2DBR(epics::pvData::ScalarType pvt);

// copy from PVField (.value sub-field) to DBF buffer
epicsShareExtern
long copyPVD2DBF(const epics::pvData::PVField::const_shared_pointer& in,
                 void *outbuf, short outdbf, long *outnReq);
// copy from DBF buffer to PVField (.value sub-field)
epicsShareExtern
long copyDBF2PVD(const epics::pvData::shared_vector<const void>& buf,
                 const epics::pvData::PVField::shared_pointer& out,
                 epics::pvData::BitSet &changed,
                 const epics::pvData::PVStringArray::const_svector& choices);

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

#ifdef EPICS_VERSION_INT
#  if EPICS_VERSION_INT>=VERSION_INT(3,16,1,0)
        epicsInt64      dbf_INT64;
        epicsUInt64     dbf_UINT64;
#  endif
#endif
        char		dbf_STRING[MAX_STRING_SIZE];
};

struct epicsShareClass DBCH {
    dbChannel *chan;
    DBCH() :chan(0) {}
    explicit DBCH(dbChannel *ch); // calls dbChannelOpen()
    explicit DBCH(const std::string& name);
    ~DBCH();

    void swap(DBCH&);

    operator dbChannel*() { return chan; }
    operator const dbChannel*() const { return chan; }
    dbChannel *operator->() { return chan; }
    const dbChannel *operator->() const { return chan; }
private:
    DBCH(const DBCH&);
    DBCH& operator=(const DBCH&);
    void prepare();
};

struct pdbRecordInfo {
    DBENTRY ent;
    pdbRecordInfo(const char *name)
    {
        dbInitEntry(pdbbase, &ent);
        if(dbFindRecordPart(&ent, &name))
            throw std::runtime_error(ent.message);
    }
    ~pdbRecordInfo()
    {
        dbFinishEntry(&ent);
    }
    const char *info(const char *key, const char *def =0)
    {
        if(dbFindInfo(&ent, key))
            return def;
        return dbGetInfoString(&ent);
    }
};

struct pdbRecordIterator {
    DBENTRY ent;
    bool m_done;
    pdbRecordIterator()
    {
        dbInitEntry(pdbbase, &ent);
        m_done = dbFirstRecordType(&ent)!=0;
        while(!m_done) {
            if(dbFirstRecord(&ent)==0)
                break;
            // not instances of this type
            m_done = dbNextRecordType(&ent)!=0;
        }
    }
    pdbRecordIterator(const dbChannel *chan)
    {
#if EPICS_VERSION_INT>=VERSION_INT(3,16,1,0)
        dbInitEntryFromRecord(dbChannelRecord(chan), &ent);
#else
        dbInitEntry(pdbbase, &ent);
        if(dbFindRecord(&ent, dbChannelRecord(chan)->name)!=0)
            throw std::logic_error("Record not found");
#endif
        m_done = false;
    }
#if EPICS_VERSION_INT>=VERSION_INT(3,16,1,0)
    pdbRecordIterator(dbCommon *prec)
    {
        dbInitEntryFromRecord(prec, &ent);
        m_done = false;
    }
#endif
    ~pdbRecordIterator()
    {
        dbFinishEntry(&ent);
    }
    bool done() const { return m_done; }
    bool next() {
        if(!m_done && dbNextRecord(&ent)!=0)
        {
            // done with this recordType
            while(true) {
                m_done = dbNextRecordType(&ent)!=0;
                if(m_done) break;
                if(dbFirstRecord(&ent)==0)
                    break;
                // not instances of this type
            }
        }
        return m_done;
    }
    dbCommon* record() const {
        return m_done ? NULL : (dbCommon*)ent.precnode->precord;
    }
    const char *name() const {
        return m_done ? NULL : ent.precnode->recordname;
    }
    const char *info(const char *key, const char *def =0)
    {
        if(m_done || dbFindInfo(&ent, key))
            return def;
        return dbGetInfoString(&ent);
    }
};

struct pdbInfoIterator {
    DBENTRY ent;
    bool m_done;
    pdbInfoIterator(const pdbRecordIterator& I)
    {
        dbCopyEntryContents(&const_cast<pdbRecordIterator&>(I).ent, &ent);
        m_done = dbFirstInfo(&ent)!=0;
    }
    ~pdbInfoIterator()
    {
        dbFinishEntry(&ent);
    }
    bool done() const { return m_done; }
    bool next() {
        m_done = dbNextInfo(&ent)!=0;
        return m_done;
    }
    const char *name() { return dbGetInfoName(&ent); }
    const char *value() { return dbGetInfoString(&ent); }
};

struct DBEvent
{
    dbEventSubscription subscript;
    unsigned dbe_mask;
    void *self;
    unsigned index;
    dbChannel *chan;
    DBEvent() :subscript(NULL), self(NULL), index(0) {}
    DBEvent(void* s) :subscript(NULL), self(s), index(0) {}
    ~DBEvent() {destroy();}
    void create(dbEventCtx ctx, dbChannel *ch, EVENTFUNC *fn, unsigned mask)
    {
        subscript = db_add_event(ctx, ch, fn, this, mask);
        if(!subscript)
            throw std::runtime_error("Failed to subscribe to dbEvent");
        chan = ch;
        dbe_mask = mask;
    }
    void destroy() {
        if(subscript) db_cancel_event(subscript);
    }
    bool operator!() const { return !subscript; }
private:
    DBEvent(const DBEvent&);
    DBEvent& operator=(const DBEvent&);
};

struct LocalFL
{
    db_field_log *pfl;
    bool ours;
    LocalFL(db_field_log *pfl, dbChannel *pchan)
        :pfl(pfl)
        ,ours(false)
    {
        if(!pfl && (ellCount(&pchan->pre_chain)!=0 || ellCount(&pchan->pre_chain)==0)) {
            pfl = db_create_read_log(pchan);
            if(pfl) {
                ours = true;
                pfl = dbChannelRunPreChain(pchan, pfl);
                if(pfl) pfl = dbChannelRunPostChain(pchan, pfl);
            }
        }
    }
    ~LocalFL() {
        if(ours) db_delete_field_log(pfl);
    }
};

struct DBScanLocker
{
    dbCommon *prec;
    DBScanLocker(dbChannel *chan) :prec(dbChannelRecord(chan))
    { dbScanLock(prec); }
    DBScanLocker(dbCommon *prec) :prec(prec)
    { dbScanLock(prec); }
    ~DBScanLocker()
    { dbScanUnlock(prec); }
};

#ifdef USE_MULTILOCK

struct DBManyLock
{
    dbLocker *plock;
    DBManyLock() :plock(NULL) {}
    DBManyLock(const std::vector<dbCommon*>& recs, unsigned flags=0)
        :plock(dbLockerAlloc((dbCommon**)&recs[0], recs.size(), flags))
    {
        if(!plock) throw std::invalid_argument("Failed to create locker");
    }
    DBManyLock(dbCommon * const *precs, size_t nrecs, unsigned flags=0)
        :plock(dbLockerAlloc((dbCommon**)precs, nrecs, flags))
    {
        if(!plock) throw std::invalid_argument("Failed to create locker");
    }
    ~DBManyLock() { if(plock) dbLockerFree(plock); }
    void swap(DBManyLock& O) { std::swap(plock, O.plock); }
    operator dbLocker*() { return plock; }
private:
    DBManyLock(const DBManyLock&);
    DBManyLock& operator=(const DBManyLock&);
};

struct DBManyLocker
{
    dbLocker *plock;
    DBManyLocker(dbLocker *L) :plock(L)
    {
        dbScanLockMany(plock);
    }
    ~DBManyLocker()
    {
        dbScanUnlockMany(plock);
    }
};
#endif

struct epicsShareClass FieldName
{
    struct Component {
        std::string name;
        epicsUInt32 index;
        Component() :index((epicsUInt32)-1) {}
        Component(const std::string& name, epicsUInt32 index = (epicsUInt32)-1)
            :name(name), index(index)
        {}
        bool isArray() const { return index!=(epicsUInt32)-1; }
    };
    typedef std::vector<Component> parts_t;
    parts_t parts;

    FieldName() {}
    explicit FieldName(const std::string&);

    void swap(FieldName& o) {
        parts.swap(o.parts);
    }

    bool empty() const { return parts.empty(); }
    size_t size() const { return parts.size(); }
    const Component& operator[](size_t i) const { return parts[i]; }
    const Component& back() const { return parts.back(); }

    // Apply field name(s) to given structure
    // if ppenclose!=NULL then the address of the enclosing field (eg. structureArray)
    // whose fieldOffset shoulbe by used, or NULL if no enclosing
    epics::pvData::PVFieldPtr
    lookup(const epics::pvData::PVStructurePtr& S, epics::pvData::PVField** ppenclose) const;

    void show() const;

#if !defined(__GNUC__) || (__GNUC__ * 100 + __GNUC_MINOR__ >= 403)
// Workaround needed for older GCC
private:
#endif
    // Prevent default copy/assignment op's
    FieldName(const FieldName&);
    FieldName& operator=(const FieldName&);
};

struct epicsShareClass PVIF {
    PVIF(dbChannel *ch);
    virtual ~PVIF() {}

    dbChannel * const chan; // borrowed reference from PVIFBuilder

    enum proc_t {
        ProcPassive,
        ProcInhibit,
        ProcForce,
    };

    //! Copy from PDB record to pvalue (call dbChannelGet())
    //! caller must lock record
    virtual void put(epics::pvData::BitSet& mask, unsigned dbe, db_field_log *pfl) =0;
    //! Copy from pvalue to PDB record (call dbChannelPut())
    //! caller must lock record
    virtual epics::pvData::Status get(const epics::pvData::BitSet& mask, proc_t proc=ProcInhibit) =0;
    //! Calculate DBE mask from changed bitset
    virtual unsigned dbe(const epics::pvData::BitSet& mask) =0;

private:
    PVIF(const PVIF&);
    PVIF& operator=(const PVIF&);
};

struct epicsShareClass PVIFBuilder {

    virtual ~PVIFBuilder() {}

    // fetch the structure description
    virtual epics::pvData::FieldConstPtr dtype(dbChannel *channel) =0;

    virtual epics::pvData::FieldBuilderPtr dtype(epics::pvData::FieldBuilderPtr& builder,
                                                 const std::string& fld,
                                                 dbChannel *channel);

    // Attach to a structure instance.
    // must be of the type returned by dtype().
    // must be the root structure
    virtual PVIF* attach(dbChannel *channel, const epics::pvData::PVStructurePtr& root, const FieldName& fld) =0;

    static PVIFBuilder* create(const std::string& name);
protected:
    PVIFBuilder() {}
private:
    PVIFBuilder(const PVIFBuilder&);
    PVIFBuilder& operator=(const PVIFBuilder&);
};

struct epicsShareClass ScalarBuilder : public PVIFBuilder
{
    virtual ~ScalarBuilder() {}

    virtual epics::pvData::FieldConstPtr dtype(dbChannel *channel) OVERRIDE FINAL;
    virtual PVIF* attach(dbChannel *channel, const epics::pvData::PVStructurePtr& root, const FieldName& fld) OVERRIDE FINAL;
};


#endif // PVIF_H
