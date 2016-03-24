
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

#include <pv/pvAccess.h>
#include <pv/clientFactory.h>

#include "helper.h"
#include "pvif.h"

namespace pvd = epics::pvData;
namespace pva = epics::pvAccess;

extern "C" void (*dbAddLinkHook)(struct link *link, short dbfType);

namespace {

typedef epicsGuard<pvd::Mutex> Guard;

struct pvaLink;
struct pvaLinkChannel;

struct pvaGlobal_t {
    pva::ChannelProvider::shared_pointer provider;

    pvd::StructureConstPtr reqtype;
    pvd::PVDataCreatePtr create;

    pvd::Mutex lock;

    typedef std::map<std::string, std::tr1::shared_ptr<pvaLinkChannel> > channels_t;
    channels_t channels;

    std::tr1::shared_ptr<pvaLinkChannel> connect(const char *name);

    pvaGlobal_t()
        :provider(pva::getChannelProviderRegistry()->getProvider("pva"))
        ,reqtype(pvd::getFieldCreate()->createFieldBuilder()
                 ->createStructure())
        ,create(pvd::getPVDataCreate())
    {
        if(!provider)
            throw std::runtime_error("No pva provider");
    }
} *pvaGlobal;

struct pvaLinkChannel : public pva::ChannelRequester, pva::MonitorRequester,
        std::tr1::enable_shared_from_this<pvaLinkChannel>
{
    const std::string name;

    typedef std::set<pvaLink*> links_t;
    links_t links;

    pvd::Mutex lock;

    pva::Channel::shared_pointer chan;

    pva::Monitor::shared_pointer chanmon;
    //pva::ChannelPut::shared_pointer chanput;

    pvd::PVStructurePtr lastval;

    pvaLinkChannel(const char *name)
        :name(name)
    {}
    virtual ~pvaLinkChannel() {
        channelStateChange(chan, pva::Channel::DESTROYED);
        if(chan) chan->destroy();
    }

    void doConnect() {
        Guard G(lock);
        chan = pvaGlobal->provider->createChannel(name, shared_from_this());
    }
    void doClose() {
        Guard G(lock);
        chan->destroy();
    }

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
        channelStateChange(channel, pva::Channel::CONNECTED);
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

std::tr1::shared_ptr<pvaLinkChannel> pvaGlobal_t::connect(const char *name)
{
    pvd::Mutex lock;

    std::tr1::shared_ptr<pvaLinkChannel> ret;
    bool doconn = false;
    {
        Guard G(lock);
        channels_t::iterator it = channels.find(name);
        if(it==channels.end()) {
            errlogPrintf("pvaLink search for '%s'\n", name);

            std::tr1::shared_ptr<pvaLinkChannel> C(new pvaLinkChannel(name));
            ret = channels[name] = C;
            doconn = true;

        } else {
            ret = it->second;
        }
    }
    if(doconn) {
        ret->doConnect();
    }
    return ret;
}

struct pvaLink
{
    link * const plink;
    const short dbf;
    std::string name, field;
    const pva::Channel::shared_pointer chan;
    bool alive;

    std::tr1::shared_ptr<pvaLinkChannel> lchan;

    pvd::PVScalarPtr valueS;
    pvd::PVScalarArray::shared_pointer valueA;
    pvd::PVScalar::shared_pointer sevr, sec, nsec;

    pvaLink(link *L, const char *name, short f)
        :plink(L)
        ,dbf(f)
        ,name(name)
        ,alive(true)
    {
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
        Guard G(lchan->lock);
        alive = false;
        detach();
        lchan->links.erase(this);
        if(lchan->links.empty()) {
            pvaGlobal->channels.erase(lchan->name);
            lchan->doClose();
        }
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
            break;
        case pvd::scalarArray:
            valueA = std::tr1::static_pointer_cast<pvd::PVScalarArray>(value);
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
};

void pvaLinkChannel::channelStateChange(pva::Channel::shared_pointer const & channel, pva::Channel::ConnectionState connectionState)
{
    Guard G(lock);
    assert(chan==channel);
    if(connectionState==pva::Channel::CONNECTED) {
        pvd::PVStructurePtr pvreq(pvaGlobal->create->createPVStructure(pvaGlobal->reqtype));

        Guard G(lock);
        chanmon = channel->createMonitor(shared_from_this(), pvreq);
        chan = channel;

    } else {
        FOREACH(it, end, links) {
            pvaLink* L = *it;
            L->detach();
        }
        lastval.reset();

        if(chanmon) {
            chanmon->destroy();
            chanmon.reset();
        }
    }
}

void pvaLinkChannel::monitorConnect(pvd::Status const & status,
                                    pva::Monitor::shared_pointer const & monitor,
                                    pvd::StructureConstPtr const & structure)
{
    if(!status.isSuccess()) {
        errlogPrintf("pvaLink connect monitor fails %s: %s\n", name.c_str(), status.getMessage().c_str());
        return;
    }
    Guard G(lock);

    lastval = pvaGlobal->create->createPVStructure(structure);
    chanmon = monitor;

    pvd::Status sstatus = monitor->start();
    if(!sstatus.isSuccess()) {
        errlogPrintf("pvaLink start monitor fails %s: %s\n", name.c_str(), sstatus.getMessage().c_str());
        return;
    }

    FOREACH(it, end, links) {
        pvaLink* L = *it;
        L->attach();
    }
}

void pvaLinkChannel::monitorEvent(pva::Monitor::shared_pointer const & monitor)
{
    Guard G(lock);

    pva::MonitorElementPtr elem;

    bool updated = false;
    while(!!(elem=monitor->poll())) {
        try{
            lastval->copyUnchecked(*elem->pvStructurePtr, *elem->changedBitSet);
            updated = true;

            monitor->release(elem);
        }catch(...){
            monitor->release(elem);
            throw;
        }
    }

    if(updated) {
//        std::cout<<"Update "<<lastval<<"\n";
        FOREACH(it, end, links) {
            pvaLink* L = *it;
            struct pv_link *ppv_link = &L->plink->value.pv_link;

            if ((ppv_link->pvlMask & pvlOptCP) ||
                    ((ppv_link->pvlMask & pvlOptCPP) && L->plink->precord->scan == 0))
            {
                // TODO: overflow once queue
                scanOnce(L->plink->precord);
            }
        }
    }
}

#define TRY pvaLink *self = (pvaLink*)plink->value.pv_link.pvt; assert(self->alive); try
#define CATCH(LOC) catch(std::exception& e) { \
    errlogPrintf("pvaLink " #LOC " fails %s: %s\n", plink->precord->name, e.what()); \
}

void pvaReportLink(const struct link *plink, dbLinkReportInfo *pinfo)
{
    const char * fname = dbGetFieldName(pinfo->pentry),
               * rname = dbGetRecordName(pinfo->pentry);

    TRY {
        pinfo->connected = self->lchan->chan && self->lchan->chanmon;

        if(pinfo->connected) {
            pinfo->readable = 1;

            if (pinfo->filter==dbLinkReportAll || pinfo->filter==dbLinkReportConnected) {
                printf(LSET_REPORT_INDENT "%28s.%-4s ==> pva://%s.%s\n",
                       rname, fname,
                       self->name.c_str(), self->field.c_str());
            }
        } else {
            if (pinfo->filter==dbLinkReportAll || pinfo->filter==dbLinkReportDisconnected) {
                printf(LSET_REPORT_INDENT "%28s.%-4s --> pva://%s.%s\n",
                       rname, fname,
                       self->name.c_str(), self->field.c_str());
            }
        }
    }CATCH(pvaReportLink)
}

void pvaRemoveLink(struct dbLocker *locker, struct link *plink)
{
    try {
        std::auto_ptr<pvaLink> self((pvaLink*)plink->value.pv_link.pvt);
        assert(self->alive);
        Guard G(self->lchan->lock);

        plink->value.pv_link.backend = NULL;
        plink->value.pv_link.pvt = NULL;
        plink->value.pv_link.pvt = 0;
        plink->value.pv_link.pvlMask = 0;
        plink->type = PV_LINK;
        plink->lset = NULL;

    }CATCH(pvaRemoteLink)
}

int pvaIsConnected(const struct link *plink)
{
    TRY {
        Guard G(self->lchan->lock);

        return !!self->lchan->chanmon && (self->valueS || self->valueA);

    }CATCH(pvaIsConnected)
    return 0;
}

int pvaGetDBFtype(const struct link *plink)
{
    TRY {
        Guard G(self->lchan->lock);
        pvd::ScalarType ftype;
        if(self->valueS)
            ftype = self->valueS->getScalar()->getScalarType();
        else if(self->valueA)
            ftype = self->valueA->getScalarArray()->getElementType();
        else
            return DBF_LONG;
        switch(ftype) {
#define MAP(DTYPE, PTYPE) case pvd::pv##PTYPE: return DBF_##DTYPE;
        MAP(CHAR, Byte);
        MAP(UCHAR, UByte);
        MAP(UCHAR, Boolean);
        MAP(SHORT, Short);
        MAP(USHORT, UShort);
        //MAP(ENUM, Int); // yes really, Base uses SHORT (16-bit) while PVD uses Int (32-bit)
        MAP(LONG, Int);
        MAP(ULONG, UInt);
        MAP(LONG, Long);  // truncation
        MAP(ULONG, ULong);// truncation
        MAP(FLOAT, Float);
        MAP(DOUBLE, Double);
        MAP(STRING, String);
#undef MAP
        }

    }CATCH(pvaIsConnected)
    return DBF_LONG;
}

long pvaGetElements(const struct link *plink, long *nelements)
{
    TRY {
        Guard G(self->lchan->lock);
        if(self->valueA)
            return self->valueA->getLength();
        else
            return 1;
    }CATCH(pvaIsConnected)
    return 1;
}

long pvaGetValue(struct link *plink, short dbrType, void *pbuffer,
        epicsEnum16 *pstat, epicsEnum16 *psevr, long *pnRequest)
{
    TRY {
        Guard G(self->lchan->lock);
        if(self->valueA) {
            //pvd::ScalarType ftype = DBR2PVD(dbrType);
            pvd::shared_vector<const void> arrval;
            self->valueA->getAs<const void>(arrval);

            long nelem = std::min(*pnRequest, (long)arrval.size());

            pvd::castUnsafeV(nelem, DBR2PVD(dbrType), pbuffer, arrval.original_type(), arrval.data());

        } else if(self->valueS) {

            switch(dbrType) {
        #define CASE(FTYPE, CTYPE) case DBR_##FTYPE: *((CTYPE*)pbuffer) = self->valueS->getAs<CTYPE>(); break
            CASE(CHAR, epicsInt8);
            CASE(UCHAR, epicsUInt8);
            CASE(SHORT, epicsInt16);
            CASE(USHORT, epicsUInt16);
            CASE(ENUM, epicsEnum16);
            CASE(LONG, epicsInt32);
            CASE(ULONG, epicsUInt32);
            CASE(FLOAT, epicsFloat32);
            CASE(DOUBLE, epicsFloat64);
        #undef CASE
            case DBR_STRING: {
                char *cbuf = (char*)pbuffer;
                strncpy(cbuf, self->valueS->getAs<std::string>().c_str(), MAX_STRING_SIZE);
                cbuf[MAX_STRING_SIZE-1] = '\0';
            }
                break;
            default:
                throw std::runtime_error("putValue unsupported DBR code");
            }
        }
        return 0;
    }CATCH(pvaIsConnected)
    return S_dbLib_badLink;
}

long pvaGetControlLimits(const struct link *plink, double *lo, double *hi)
{
    TRY {
        Guard G(self->lchan->lock);
        *lo = *hi = 0.0;
    }CATCH(pvaIsConnected)
    return 0;
}

long pvaGetGraphicLimits(const struct link *plink, double *lo, double *hi)
{
    TRY {
        Guard G(self->lchan->lock);
        *lo = *hi = 0.0;
    }CATCH(pvaIsConnected)
    return 0;
}

long pvaGetAlarmLimits(const struct link *plink, double *lolo, double *lo,
        double *hi, double *hihi)
{
    TRY {
        Guard G(self->lchan->lock);
        *lolo = *lo = *hi = *hihi = 0.0;
    }CATCH(pvaIsConnected)
    return 0;
}

long pvaGetPrecision(const struct link *plink, short *precision)
{
    TRY {
        Guard G(self->lchan->lock);
        *precision = 0;
    }CATCH(pvaIsConnected)
    return 0;
}

long pvaGetUnits(const struct link *plink, char *units, int unitsSize)
{
    TRY {
        Guard G(self->lchan->lock);
    }CATCH(pvaIsConnected)
    return 0;
}

long pvaGetAlarm(const struct link *plink, epicsEnum16 *status,
        epicsEnum16 *severity)
{
    TRY {
        Guard G(self->lchan->lock);
        unsigned sevr = 0;
        if(self->sevr)
            sevr = self->sevr->getAs<epicsInt32>();
        if(sevr)
            *status = LINK_ALARM;
        *severity = std::max(0u, std::min(sevr, 3u));
    }CATCH(pvaIsConnected)
    return 0;
}

long pvaGetTimeStamp(const struct link *plink, epicsTimeStamp *pstamp)
{
    TRY {
        Guard G(self->lchan->lock);
        if(self->sec && self->nsec) {
            pstamp->secPastEpoch = self->sec->getAs<epicsUInt32>()-POSIX_TIME_AT_EPICS_EPOCH;
            pstamp->nsec = self->sec->getAs<epicsUInt32>();
        } else {
            epicsTimeGetCurrent(pstamp);
        }
    }CATCH(pvaIsConnected)
    return 0;
}

long pvaPutValue(struct link *plink, short dbrType,
        const void *pbuffer, long nRequest)
{
    TRY {
        (void)self;
        //Guard G(self->lchan->lock);
        return S_db_putDisabled;
    }CATCH(pvaIsConnected)
}

void pvaScanForward(struct link *plink)
{
    TRY {
        (void)self;
        //Guard G(self->lchan->lock);
    }CATCH(pvaIsConnected)
}

lset pva_lset = {
    LSET_API_VERSION,
    &pvaReportLink,
    &pvaRemoveLink,
    &pvaIsConnected,
    &pvaGetDBFtype,
    &pvaGetElements,
    &pvaGetValue,
    &pvaGetControlLimits,
    &pvaGetGraphicLimits,
    &pvaGetAlarmLimits,
    &pvaGetPrecision,
    &pvaGetUnits,
    &pvaGetAlarm,
    &pvaGetTimeStamp,
    &pvaPutValue,
    &pvaScanForward
};

static
void (*nextAddLinkHook)(struct link *link, short dbfType);

void pvaAddLinkHook(struct link *link, short dbfType)
{
    const char *target = link->value.pv_link.pvname;

    if(strncmp(target, "pva://", 6)!=0) {
        if(nextAddLinkHook)
            (*nextAddLinkHook)(link, dbfType);
        return;
    }

    assert(link->precord);
    assert(link->type == PV_LINK);

    target += 6; // skip "pva://"

    try {

        std::cerr<<"pvaLink '"<<target<<"'\n";

        std::auto_ptr<pvaLink> pvt(new pvaLink(link, target, dbfType));

        link->value.pv_link.pvt = pvt.release();
        link->value.pv_link.backend = "pva";
        link->type = CA_LINK;
        link->lset = &pva_lset;

    }catch(std::exception& e){
        errlogPrintf("Error setting up pva link: %s : %s\n", link->value.pv_link.pvname, e.what());
        // return w/ lset==NULL results in an invalid link (all operations error)
    }
}

void initPVALink(initHookState state)
{
    if(state==initHookAfterCaLinkInit) {
        try {
            std::cout<<"initPVALink\n";

            pva::ClientFactory::start();

            pvaGlobal = new pvaGlobal_t;

            nextAddLinkHook = dbAddLinkHook;
            dbAddLinkHook = &pvaAddLinkHook;
        }catch(std::exception& e){
            errlogPrintf("Error initializing pva link handling : %s\n", e.what());
        }

    } else if(state==initHookAfterCaLinkClose) {
        try {
            std::cout<<"cleanupPVALink\n";
            dbAddLinkHook = nextAddLinkHook;

            delete pvaGlobal;
            pvaGlobal = NULL;

        }catch(std::exception& e){
            errlogPrintf("Error initializing pva link handling : %s\n", e.what());
        }

    }
}

void installPVAAddLinkHook()
{
    initHookRegister(&initPVALink);
}

} // namespace

epicsExportRegistrar(installPVAAddLinkHook);
