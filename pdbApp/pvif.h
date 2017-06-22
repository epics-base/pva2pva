#ifndef PVIF_H
#define PVIF_H

#include <map>

#include <dbAccess.h>
#include <dbChannel.h>
#include <dbStaticLib.h>
#include <dbLock.h>
#include <dbEvent.h>

#include <pv/bitSet.h>
#include <pv/pvData.h>

epics::pvData::ScalarType DBR2PVD(short dbr);
short PVD2DBR(epics::pvData::ScalarType pvt);

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

struct DBCH {
    dbChannel *chan;
    DBCH() :chan(0) {}
    explicit DBCH(dbChannel *ch); // calls dbChannelOpen()
    explicit DBCH(const std::string& name);
    explicit DBCH(const char *name);
    ~DBCH();

    void swap(DBCH&);

    operator dbChannel*() { return chan; }
    dbChannel *operator->() { return chan; }
private:
    DBCH(const DBCH&);
    DBCH& operator=(const DBCH&);
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
        dbInitEntry(pdbbase, &ent);
        if(dbFindRecord(&ent, dbChannelRecord(chan)->name)!=0)
            throw std::runtime_error("Record not found");
        m_done = false;
    }
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
        return m_done ? NULL : ((dbCommon*)ent.precnode->precord)->name;
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

struct DBManyLock
{
    dbLocker *plock;
    DBManyLock() :plock(NULL) {}
    DBManyLock(const std::vector<dbCommon*>& recs, unsigned flags)
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

struct PVIF {
    PVIF(dbChannel *ch, const epics::pvData::PVStructurePtr& p);
    virtual ~PVIF() {}

    dbChannel *chan;
    epics::pvData::PVStructurePtr pvalue;

    //! Copy from PDB record to pvalue (call dbChannelGet())
    //! caller must lock record
    virtual void put(epics::pvData::BitSet& mask, unsigned dbe, db_field_log *pfl) =0;
    //! Copy from pvalue to PDB record (call dbChannelPut())
    //! caller must lock record
    virtual void get(const epics::pvData::BitSet& mask) =0;

    static void Init();

    // fetch the structure description for a DBR type
    static epics::pvData::StructureConstPtr dtype(dbChannel *chan);

    // Create a PVIF associating the given channel to the given PVStructure node (may not be actual root)
    static PVIF* attach(dbChannel* ch, const epics::pvData::PVStructurePtr& root);
};

#endif // PVIF_H
