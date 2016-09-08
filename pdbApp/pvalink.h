#ifndef PVALINK_H
#define PVALINK_H

#include <set>
#include <map>

#define EPICS_DBCA_PRIVATE_API
#include <epicsGuard.h>
#include <dbAccess.h>
#include <dbCommon.h>
#include <dbLink.h>
#include <dbScan.h>
#include <epicsExport.h>
#include <errlog.h>
#include <initHooks.h>
#include <alarm.h>
#include <epicsExit.h>
#include <epicsAtomic.h>
#include <epicsThreadPool.h>
#include <link.h>
#include <dbJLink.h>

#include <pv/pvAccess.h>
#include <pv/clientFactory.h>

#include "helper.h"
#include "iocshelper.h"
#include "pvif.h"

extern int pvaLinkDebug;
extern int pvaLinkIsolate;

namespace pvalink {

namespace pvd = epics::pvData;
namespace pva = epics::pvAccess;

typedef epicsGuard<pvd::Mutex> Guard;
typedef epicsGuardRelease<pvd::Mutex> UnGuard;

struct pvaLink;
struct pvaLinkChannel;

struct pvaGlobal_t {
    pva::ChannelProvider::shared_pointer provider;

    pvd::StructureConstPtr reqtype;
    pvd::PVDataCreatePtr create;

    pvd::Mutex lock;

    struct Scan {
        // the PVA channel which triggered this scan
        std::tr1::weak_ptr<pvaLinkChannel> chan;
        bool usecached;
        Scan() :usecached(false) {}
    };

    epicsThreadPrivate<Scan> scanmagic;
    epicsThreadPool *scanpool;

    typedef std::map<std::string, std::tr1::shared_ptr<pvaLinkChannel> > channels_t;
    channels_t channels;

    std::tr1::shared_ptr<pvaLinkChannel> connect(const char *name);

    pvaGlobal_t();
    ~pvaGlobal_t()
    {
        provider->destroy();
        epicsThreadPoolDestroy(scanpool);
    }
};
extern pvaGlobal_t *pvaGlobal;

struct pvaLinkChannel : public pva::ChannelRequester, pva::MonitorRequester,
        std::tr1::enable_shared_from_this<pvaLinkChannel>
{
    const std::string name;

    static size_t refs;

    typedef std::set<pvaLink*> links_t;
    links_t links;

    pvd::Mutex lock;

    pva::Channel::shared_pointer chan;

    pva::Monitor::shared_pointer chanmon;
    //pva::ChannelPut::shared_pointer chanput;

    pvd::PVStructurePtr lastval;
    pvd::PVScalarPtr isatomic;

    epicsJob *scanjob;
    std::tr1::shared_ptr<pvaLinkChannel> scanself; // create ref loop while scan is queued
    bool scanatomic;

    pvaLinkChannel(const char *name)
        :name(name)
        ,scanjob(epicsJobCreate(pvaGlobal->scanpool, &pvaLinkChannel::scan, this))
        ,scanatomic(false)
    {
        if(!scanjob)
            throw std::runtime_error("failed to create job for pvaLink");
        epics::atomic::increment(refs);
    }
    virtual ~pvaLinkChannel() {
        Guard G(lock);
        assert(links.empty());
        epicsJobDestroy(scanjob);
        scanjob = NULL;
        epics::atomic::decrement(refs);
        std::cerr<<"pvaLinkChannel: destroy "<<name<<"\n";
    }

    void doConnect() {
        // TODO: local PVA?
        Guard G(lock);
        chan = pvaGlobal->provider->createChannel(name, shared_from_this());
        channelStateChange(chan, chan->getConnectionState());
    }
    void doClose() {
        Guard G(lock);
        errlogPrintf("pvaLink closing %s\n", name.c_str());
        channelStateChange(chan, pva::Channel::DESTROYED);
        chan->destroy();
        chan.reset();
        std::cerr<<"pvaLink: channel destroy "<<name<<"\n";
    }

    void triggerProc(bool atomic=false, bool force=false);

    static void scan(void* arg, epicsJobMode mode);

    virtual std::string getRequesterName() { return "pvaLink"; }
    virtual void message(std::string const & message, pva::MessageType messageType)
    {
        errlogPrintf("%s pvaLink \"%s\": %s\n",
                     pvd::getMessageTypeName(messageType).c_str(),
                     name.c_str(),
                     message.c_str());
    }

    virtual void channelCreated(const epics::pvData::Status& status, pva::Channel::shared_pointer const & channel)
    {
        if(!status.isSuccess()) {
            errlogPrintf("pvaLink create fails %s: %s\n", name.c_str(), status.getMessage().c_str());
            return;
        }
        Guard G(lock);
        //assert(chan==channel); // may be called before createChannel() returns
        chan = channel;
    }

    virtual void channelStateChange(pva::Channel::shared_pointer const & channel, pva::Channel::ConnectionState connectionState);

    virtual void monitorConnect(pvd::Status const & status,
                                pva::Monitor::shared_pointer const & monitor,
                                pvd::StructureConstPtr const & structure);

    virtual void monitorEvent(pva::Monitor::shared_pointer const & monitor);

    virtual void unlisten(pva::Monitor::shared_pointer const & monitor)
    {
        // what to do??
    }

};

struct pvaLink : public jlink
{
    static size_t refs;

    DBLINK * plink; // may be NULL
    unsigned linkmods;
    unsigned parse_level;

    std::string name, field;
    const pva::Channel::shared_pointer chan;
    bool alive; // attempt to catch some use after free

    std::tr1::shared_ptr<pvaLinkChannel> lchan;

    pvd::PVScalarPtr valueS;
    pvd::PVScalarArray::shared_pointer valueA;
    pvd::PVScalar::shared_pointer sevr, sec, nsec;
    pvd::ScalarType etype;

    struct Value {
        bool valid;
        bool scalar;
        pvd::ScalarType etype;
        pvd::shared_vector<const void> valueA;
        dbrbuf valueS;
        epicsUInt16 sevr;
        epicsTimeStamp time;
        Value() :valid(false) {}
        void clear() {
            valid = false;
            valueA.clear();
        }
    };

    Value atomcache;

    pvaLink()
        :plink(0)
        ,linkmods(0)
        ,parse_level(0)
        ,alive(true)
    {
        epics::atomic::increment(refs);
        //TODO: valgrind tells me these aren't initialized by Base, but probably should be.
        parseDepth = 0;
        parent = 0;
    }

    void open()
    {
        if(this->name.empty())
            throw std::logic_error("open() w/o target PV name");
        this->name = name;
        size_t dot = this->name.find_first_of('.');
        if(dot!=this->name.npos) {
            field = this->name.substr(dot+1);
            this->name = this->name.substr(0, dot);
        }
        lchan = pvaGlobal->connect(this->name.c_str());
        Guard G(lchan->lock);
        lchan->links.insert(this);
        if(lchan->lastval)
            attach();
    }
    ~pvaLink()
    {
        alive = false;
        if(lchan) { // may be NULL if parsing fails
            Guard G(lchan->lock);
            detach();
            lchan->links.erase(this);
            if(lchan->links.empty()) {
                pvaGlobal->channels.erase(lchan->name);
                lchan->doClose();
            }
        }
        epics::atomic::decrement(refs);
    }

    void detach()
    {
        valueS.reset();
        valueA.reset();
        sevr.reset();
        sec.reset();
        nsec.reset();
    }

    bool attach()
    {
        pvd::PVStructurePtr base(lchan->lastval);

        if(!field.empty())
            base = base->getSubField<pvd::PVStructure>(field);
        if(!base) {
            errlogPrintf("pvaLink not %s%c%s\n", name.c_str(), field.empty() ? ' ' : '.', field.c_str());
            return false;
        }

        pvd::PVFieldPtr value(base->getSubField("value"));
        switch(value->getField()->getType())
        {
        case pvd::scalar:
            valueS = std::tr1::static_pointer_cast<pvd::PVScalar>(value);
            etype = valueS->getScalar()->getScalarType();
            break;
        case pvd::scalarArray:
            valueA = std::tr1::static_pointer_cast<pvd::PVScalarArray>(value);
            etype = valueA->getScalarArray()->getElementType();
            break;
        default:
            errlogPrintf("pvaLink not .value : %s%c%s\n", name.c_str(), field.empty() ? ' ' : '.', field.c_str());
            return false;
        }

        sevr = base->getSubField<pvd::PVScalar>("alarm.severity");
        sec = base->getSubField<pvd::PVScalar>("timeStamp.secondsPastEpoch");
        nsec = base->getSubField<pvd::PVScalar>("timeStamp.nanoseconds");
        return true;
    }

    void get(Value& v)
    {
        if(valueA) {
            valueA->getAs<const void>(v.valueA);
            v.etype = v.valueA.original_type();
            v.scalar = false;

        } else if(valueS) {

            switch(etype) {
#define CASE(BASETYPE, PVATYPE, DBFTYPE, PVACODE) case pvd::pv ## PVACODE: v.valueS.dbf_##DBFTYPE = valueS->getAs<PVATYPE>(); break;
#define CASE_SQUEEZE_INT64
#include "pvatypemap.h"
#undef CASE_SQUEEZE_INT64
#undef CASE
            case pvd::pvString: {
                strncpy(v.valueS.dbf_STRING, valueS->getAs<std::string>().c_str(), sizeof(v.valueS.dbf_STRING));
                v.valueS.dbf_STRING[sizeof(v.valueS.dbf_STRING)-1] = '\0';
            }
                break;
            default:
                throw std::runtime_error("putValue unsupported DBR code");
            }

            v.etype = etype;
            v.scalar = true;
        }

        v.sevr = sevr->getAs<epicsUInt16>();
        v.time.secPastEpoch = sec->getAs<epicsUInt32>()-POSIX_TIME_AT_EPICS_EPOCH;
        v.time.nsec = nsec->getAs<epicsUInt32>();
        v.valid = true;
    }
};


} // namespace pvalink

#endif // PVALINK_H
