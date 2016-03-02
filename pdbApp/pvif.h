#ifndef PVIF_H
#define PVIF_H

#include <map>

#include <dbAccess.h>
#include <dbChannel.h>
#include <dbStaticLib.h>
#include <dbLock.h>

#include <pv/bitSet.h>
#include <pv/pvData.h>

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
        if(!m_done) m_done = dbFirstRecord(&ent)!=0;
    }
    ~pdbRecordIterator()
    {
        dbFinishEntry(&ent);
    }
    bool done() const { return m_done; }
    bool next() {
        m_done = dbNextRecord(&ent)!=0;
        if(m_done) m_done = dbNextRecordType(&ent)!=0;
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
    virtual void get(epics::pvData::BitSet& mask) =0;

    static void Init();

    // fetch the structure description for a DBR type
    static epics::pvData::StructureConstPtr dtype(dbChannel *chan);

    // Create a PVIF associating the given channel to the given PVStructure node (may not be actual root)
    static PVIF* attach(dbChannel* ch, const epics::pvData::PVStructurePtr& root);
};

#endif // PVIF_H
