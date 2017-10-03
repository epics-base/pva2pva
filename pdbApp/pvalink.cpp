
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
#include "pvalink.h"

int pvaLinkDebug;
int pvaLinkIsolate;

namespace pvalink {

pvaGlobal_t *pvaGlobal;

std::tr1::shared_ptr<pvaLinkChannel> pvaGlobal_t::connect(const char *name)
{
    pvd::Mutex lock;

    std::tr1::shared_ptr<pvaLinkChannel> ret;
    bool doconn = false;
    {
        Guard G(lock);
        channels_t::iterator it = channels.find(name);
        if(it==channels.end()) {
            if(pvaLinkDebug>2) std::cerr<<"pvalink open channel for '"<<name<<"'\n";

            std::tr1::shared_ptr<pvaLinkChannel> C(new pvaLinkChannel(name));
            ret = channels[name] = C;
            doconn = true;

        } else {
            if(pvaLinkDebug>2) std::cerr<<"pvalink reuse channel for '"<<name<<"'\n";
            ret = it->second;
        }
    }
    if(doconn) {
        ret->doConnect();
    }
    return ret;
}

pvaGlobal_t::pvaGlobal_t()
    :provider(pva::getChannelProviderRegistry()->getProvider(pvaLinkIsolate ? "QSRV" : "pva"))
    ,reqtype(pvd::getFieldCreate()->createFieldBuilder()
             ->createStructure())
    ,create(pvd::getPVDataCreate())
{
    if(!provider)
        throw std::runtime_error("No pva provider");
    epicsThreadPoolConfig conf;
    epicsThreadPoolConfigDefaults(&conf);
    conf.workerPriority = epicsThreadPriorityLow+10; // similar to once thread
    conf.initialThreads = 1;
    scanpool = epicsThreadPoolCreate(&conf);
    if(!scanpool)
        throw std::runtime_error("Failed to create pvaLink scan pool");
}

size_t pvaLinkChannel::refs;
size_t pvaLink::refs;

void pvaLinkChannel::channelStateChange(pva::Channel::shared_pointer const & channel, pva::Channel::ConnectionState connectionState)
{
    Guard G(lock);
    assert(chan==channel);
    if(pvaLinkDebug>2) std::cerr<<"pvaLink channelStateChange "<<name<<pva::Channel::ConnectionStateNames[connectionState]<<"\n";
    if(connectionState!=pva::Channel::CONNECTED) {
        FOREACH(it, end, links) {
            pvaLink* L = *it;
            L->detach();
        }
        lastval.reset();
        isatomic.reset();

        if(chanmon) {
            chanmon->destroy();
            chanmon.reset();
            std::cerr<<"pvaLink: monitor destroy "<<name<<"\n";
        }
        triggerProc(false, true); // force scan to get disconnect
    } else if(!chanmon) {
        pvd::PVStructurePtr pvreq(pvaGlobal->create->createPVStructure(pvaGlobal->reqtype));

        Guard G(lock);
        chanmon = channel->createMonitor(shared_from_this(), pvreq);
        chan = channel;
        if(pvaLinkDebug>4) std::cerr<<"pvaLink channelStateChange start monitor\n";
    }
}

void pvaLinkChannel::monitorConnect(pvd::Status const & status,
                                    pva::Monitor::shared_pointer const & monitor,
                                    pvd::StructureConstPtr const & structure)
{
    if(pvaLinkDebug>4) std::cerr<<"pvaLink monitorConnect "<<name<<"\n";
    if(!status.isSuccess()) {
        errlogPrintf("pvaLink connect monitor fails %s: %s\n", name.c_str(), status.getMessage().c_str());
        return;
    }
    Guard G(lock);

    lastval = pvaGlobal->create->createPVStructure(structure);
    isatomic = lastval->getSubField<pvd::PVScalar>("record._options.atomic");
    if(!isatomic)
        std::cerr<<"================ not atomic\n"<<lastval<<"\n";
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
    if(pvaLinkDebug>3) std::cerr<<"pvaLink monitorEvent "<<name<<"\n";
    if(!lastval) return;

    pva::MonitorElementPtr elem;

    bool updated = false;
    bool atomic = false;
    while(!!(elem=monitor->poll())) {
        try{
            lastval->copyUnchecked(*elem->pvStructurePtr, *elem->changedBitSet);
            atomic = isatomic ? isatomic->getAs<pvd::boolean>() : false;
            updated = true;

            monitor->release(elem);
        }catch(...){
            monitor->release(elem);
            throw;
        }
    }

    if(updated) triggerProc(atomic);
}

// caller must have channel's lock
void pvaLinkChannel::triggerProc(bool atomic, bool force)
{
    bool doscan = false;
    // check if we actually need to scan anything
    FOREACH(it, end, links) {
        pvaLink* L = *it;

        if ((L->linkmods & pvlOptCP) ||
                ((L->linkmods & pvlOptCPP) && L->plink->precord->scan == 0))
        {
            doscan = true;
        }
    }
    if(force || (doscan && !scanself)) { // need to scan, and not already queued, then queue
        int ret = epicsJobQueue(scanjob);
        if(ret && ret!=S_pool_paused) {
            errlogPrintf("pvaLink: failed to queue scan from %s\n", name.c_str());
        } else {
            scanself = shared_from_this();
            scanatomic = atomic;
            if(pvaLinkDebug>1) std::cerr<<"pvaLink trigger proc"<< name<<"\n";
        }
    }
}

void pvaLinkChannel::scan(void* arg, epicsJobMode mode)
{
    pvaLinkChannel *selfraw = (pvaLinkChannel*)arg;
    if(mode!=epicsJobModeRun) return; // we will cleanup later
    pvaGlobal_t::Scan myscan;
    try {
        if(pvaLinkDebug>3) std::cerr<<"pvaLink scan "<<selfraw->name<<"\n";

        std::tr1::shared_ptr<pvaLinkChannel> self;

        Guard G(selfraw->lock);

        selfraw->scanself.swap(self); // we take over ref, to keep channel alive, and allow re-queue
        assert(self.get()==selfraw); // if scanself wasn't set, then the channel may be free'd

        myscan.chan = self; // store a weak ref

        links_t links(self->links); // TODO: avoid copy if set not changing

        bool usecached = self->scanatomic && !!self->chanmon;
        myscan.usecached = usecached;
        if(usecached) {
            if(pvaLinkDebug>4) std::cerr<<"populate cache\n";
            FOREACH(it, end, links) {
                pvaLink *link = *it;
                link->get(link->atomcache);
                if(pvaLinkDebug>4)
                    std::cerr<<"== "<<self->name<<"."<<link->field<<" "<<link->valueS<<"\n";
            }
        }

        pvaGlobal->scanmagic.set(usecached ? &myscan : NULL);

        {
            UnGuard U(G);
            // we may scan a record after the originating link is re-targeted

            FOREACH(it, end, links) {
                pvaLink *link = *it;
                dbCommon *prec=link->plink->precord;

                if ((link->linkmods & pvlOptCP) ||
                        ((link->linkmods & pvlOptCPP) && prec->scan == 0))
                {
                    DBScanLocker L(prec);
                    if(pvaLinkDebug>3) std::cerr<<prec->name<<" PVA link scan\n";
                    dbProcess(prec);
                }
            }
        }
        // another scan may be queued by this point

        if(usecached) {
            FOREACH(it, end, links) {
                pvaLink *link = *it;
                link->atomcache.clear();
            }
        }

    }catch(std::exception& e){
        errlogPrintf("%s: pvaLink exception while processing: %s\n", selfraw->name.c_str(), e.what());
        // what to do?
    }
    pvaGlobal->scanmagic.set(NULL);
}


} // namespace pvalink

using namespace pvalink;

namespace {

#define TRY pvaLink *self = static_cast<pvaLink*>(plink->value.json.jlink); assert(self->alive); try
#define CATCH(LOC) catch(std::exception& e) { \
    errlogPrintf("pvaLink " #LOC " fails %s: %s\n", plink->precord->name, e.what()); \
}

void pvaOpenLink(DBLINK *plink)
{
    try {
        pvaLink* self((pvaLink*)plink->value.json.jlink);

        self->plink = plink;
        std::cerr<<plink->precord->name<<" Open link to '"<<self->name<<"'\n";
        if(!self->name.empty()) {
            self->open();
        }

    }CATCH(pvaOpenLink)
}

void pvaRemoveLink(struct dbLocker *locker, DBLINK *plink)
{
    try {
        p2p::auto_ptr<pvaLink> self((pvaLink*)plink->value.json.jlink);
        assert(self->alive);
        Guard G(self->lchan->lock);

        // TODO: ???
        std::cerr<<__FUNCTION__<<" "<<self->plink->precord->name<<" -> "<<self->name<<"\n";

    }CATCH(pvaRemoteLink)
}

int pvaIsConnected(const DBLINK *plink)
{
    TRY {
        if(pvaGlobal->scanmagic.get()) return 1;

        Guard G(self->lchan->lock);

        return !!self->lchan->chanmon && (self->valueS || self->valueA);

    }CATCH(pvaIsConnected)
    return 0;
}

int pvaGetDBFtype(const DBLINK *plink)
{
    TRY {
        if(pvaGlobal->scanmagic.get() && self->atomcache.valid)
            return PVD2DBR(self->atomcache.etype);

        Guard G(self->lchan->lock);
        pvd::ScalarType ftype;
        if(self->valueS)
            ftype = self->valueS->getScalar()->getScalarType();
        else if(self->valueA)
            ftype = self->valueA->getScalarArray()->getElementType();
        else
            return DBF_LONG;
        switch(ftype) {
#define CASE(BASETYPE, PVATYPE, DBFTYPE, PVACODE) case pvd::pv##PVACODE: return DBF_##DBFTYPE;
#define CASE_SQUEEZE_INT64
#include "pv/typemap.h"
#undef CASE_SQUEEZE_INT64
#undef CASE
        case pvd::pvString: return DBF_STRING; // TODO: long string?
        }

    }CATCH(pvaIsConnected)
    return DBF_LONG;
}

long pvaGetElements(const DBLINK *plink, long *nelements)
{
    TRY {
        if(pvaGlobal->scanmagic.get() && self->atomcache.valid) {
            if(self->atomcache.scalar) return 1;
            else return self->atomcache.valueA.size();
        }

        Guard G(self->lchan->lock);
        if(self->valueA)
            return self->valueA->getLength();
        else
            return 1;
    }CATCH(pvaIsConnected)
    return 1;
}

long pvaGetValue(DBLINK *plink, short dbrType, void *pbuffer,
        long *pnRequest)
{
    TRY {
        if(pvaGlobal->scanmagic.get() && self->atomcache.valid) {
            const void *buf;
            size_t count = pnRequest ? *pnRequest : 1;
            if(self->atomcache.scalar) {
                buf = (void*)&self->atomcache.valueS;
                count = std::min((size_t)1u, count);
            } else {
                buf = self->atomcache.valueA.data();
                count = std::min(self->atomcache.valueA.size(), count);
            }

            pvd::castUnsafeV(count, DBR2PVD(dbrType), pbuffer, self->atomcache.etype, buf);
//            if(dbrType==DBF_DOUBLE) {
//                std::cerr<<"get from cache "<<*(double*)pbuffer<<"\n";
//            }
            if(pnRequest) *pnRequest = count;
            return 0;
        }

        Guard G(self->lchan->lock);
        if(self->valueA) {
            pvd::shared_vector<const void> arrval;
            self->valueA->getAs<const void>(arrval);

            long nelem = std::min(*pnRequest, (long)arrval.size());

            pvd::castUnsafeV(nelem, DBR2PVD(dbrType), pbuffer, arrval.original_type(), arrval.data());
            if(pnRequest) *pnRequest = nelem;

        } else if(self->valueS) {

            switch(dbrType) {
#define CASE(BASETYPE, PVATYPE, DBFTYPE, PVACODE) case DBR_##DBFTYPE: *((epics##BASETYPE*)pbuffer) = self->valueS->getAs<epics##BASETYPE>(); break;
#define CASE_SKIP_BOOL
#define CASE_ENUM
#include "pv/typemap.h"
#undef CASE_SKIP_BOOL
#undef CASE_ENUM
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
            if(pnRequest) *pnRequest = 1;

            if(dbrType==DBF_DOUBLE)
                std::cerr<<"get direct "<<*(double*)pbuffer<<"\n";
        } else {
            return -1;
        }
        return 0;
    }CATCH(pvaIsConnected)
    return S_dbLib_badLink;
}

long pvaGetControlLimits(const DBLINK *plink, double *lo, double *hi)
{
    TRY {
        //Guard G(self->lchan->lock);
        *lo = *hi = 0.0;
    }CATCH(pvaIsConnected)
    return 0;
}

long pvaGetGraphicLimits(const DBLINK *plink, double *lo, double *hi)
{
    TRY {
        //Guard G(self->lchan->lock);
        *lo = *hi = 0.0;
    }CATCH(pvaIsConnected)
    return 0;
}

long pvaGetAlarmLimits(const DBLINK *plink, double *lolo, double *lo,
        double *hi, double *hihi)
{
    TRY {
        //Guard G(self->lchan->lock);
        *lolo = *lo = *hi = *hihi = 0.0;
    }CATCH(pvaIsConnected)
    return 0;
}

long pvaGetPrecision(const DBLINK *plink, short *precision)
{
    TRY {
        //Guard G(self->lchan->lock);
        *precision = 0;
    }CATCH(pvaIsConnected)
    return 0;
}

long pvaGetUnits(const DBLINK *plink, char *units, int unitsSize)
{
    TRY {
        //Guard G(self->lchan->lock);
    }CATCH(pvaIsConnected)
    return 0;
}

long pvaGetAlarm(const DBLINK *plink, epicsEnum16 *status,
        epicsEnum16 *severity)
{
    TRY {
        Guard G(self->lchan->lock);
        unsigned sevr = INVALID_ALARM;
        if(pvaGlobal->scanmagic.get() && self->atomcache.valid) {
            sevr = self->atomcache.sevr;
        } else if(self->sevr) {
            sevr = self->sevr->getAs<epicsInt32>();
        }
        if(sevr)
            *status = LINK_ALARM;
        *severity = std::max(0u, std::min(sevr, 3u));
    }CATCH(pvaIsConnected)
    return 0;
}

long pvaGetTimeStamp(const DBLINK *plink, epicsTimeStamp *pstamp)
{
    TRY {
        Guard G(self->lchan->lock);
        if(pvaGlobal->scanmagic.get() && self->atomcache.valid) {
            *pstamp = self->atomcache.time;
        } else if(self->sec && self->nsec) {
            pstamp->secPastEpoch = self->sec->getAs<epicsUInt32>()-POSIX_TIME_AT_EPICS_EPOCH;
            pstamp->nsec = self->sec->getAs<epicsUInt32>();
        } else {
            epicsTimeGetCurrent(pstamp);
        }
    }CATCH(pvaIsConnected)
    return 0;
}

long pvaPutValue(DBLINK *plink, short dbrType,
        const void *pbuffer, long nRequest)
{
    TRY {
        (void)self;
        //Guard G(self->lchan->lock);
        return S_db_putDisabled;
    }CATCH(pvaIsConnected)
}

void pvaScanForward(DBLINK *plink)
{
    TRY {
        (void)self;
        //Guard G(self->lchan->lock);
    }CATCH(pvaIsConnected)
}

#undef TRY
#undef CATCH

lset pva_lset = {
    0, 1, // non-const, volatile
    &pvaOpenLink,
    &pvaRemoveLink,
    NULL, NULL, NULL,
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
    NULL,
    &pvaScanForward
    //&pvaReportLink,
};

static void stopPVAPool(void*)
{
    // stop CP scans before closing links
    epicsThreadPoolControl(pvaGlobal->scanpool, epicsThreadPoolQueueAdd, 0);
    epicsThreadPoolWait(pvaGlobal->scanpool, -1.0);
}

static void finalizePVA(void*)
{
    try {
        std::cout<<"cleanupPVALink\n";
        //dbAddLinkHook = nextAddLinkHook;

        {
            Guard G(pvaGlobal->lock);
            if(pvaGlobal->channels.size()) {
                std::cerr<<"pvaLink still has "<<pvaGlobal->channels.size()
                        <<"active channels after doCloseLinks()\n";
            }
        }

        epicsThreadPoolDestroy(pvaGlobal->scanpool);
        pvaGlobal->scanpool = NULL; // TODO: locking?
        delete pvaGlobal;
        pvaGlobal = NULL;

        if(epics::atomic::get(pvaLink::refs)) {
            std::cerr<<"pvaLink leaking "<<epics::atomic::get(pvaLink::refs)<<" links\n";
        }
        if(epics::atomic::get(pvaLinkChannel::refs)) {
            std::cerr<<"pvaLink leaking "<<epics::atomic::get(pvaLinkChannel::refs)<<" channels\n";
        }

    }catch(std::exception& e){
        errlogPrintf("Error initializing pva link handling : %s\n", e.what());
    }
}

void initPVALink(initHookState state)
{
    if(state==initHookAfterCaLinkInit) {
        // before epicsExit(exitDatabase)
        // so hook registered here will be run after iocShutdown()
        // which closes links
        try {
            std::cout<<"initPVALink\n";

            pva::ClientFactory::start();

            pvaGlobal = new pvaGlobal_t;

            epicsAtExit(finalizePVA, NULL);

            //nextAddLinkHook = dbAddLinkHook;
            //dbAddLinkHook = &pvaAddLinkHook;
        }catch(std::exception& e){
            errlogPrintf("Error initializing pva link handling : %s\n", e.what());
        }

    } else if(state==initHookAfterIocBuilt) {
        // after epicsExit(exitDatabase)
        // so hook registered here will be run before iocShutdown()
        epicsAtExit(stopPVAPool, NULL);
    }
}

jlink* pva_alloc_jlink(short dbr)
{
    try {

        std::cerr<<"alloc jlink\n";
        return new pvaLink;

    }catch(std::exception& e){
        return NULL;
    }
}

#define TRY  pvaLink *pvt = static_cast<pvaLink*>(pjlink); (void)pvt; try
#define CATCH(RET) catch(std::exception& e){ \
    errlogPrintf("Error in %s link: %s\n", __FUNCTION__, e.what()); \
    return RET; }

void pva_free_jlink(jlink *pjlink)
{
    //TODO: not called on parse error?
    TRY {
        std::cerr<<"free jlink\n";
        delete pvt;
    }catch(std::exception& e){
        errlogPrintf("Error freeing pva link: %s\n", e.what());
    }
}

void pva_end_child(jlink *pjparent, jlink *pjlink)
{}

jlif_result pva_parse_null(jlink *pjlink)
{
    TRY{
        std::cerr<<"NULL parse\n";
        return jlif_stop;
    }CATCH(jlif_stop)
}

jlif_result pva_parse_boolean(jlink *pjlink, int val)
{
    TRY{
        std::cerr<<"bool parse\n";
        return jlif_stop;
    }CATCH(jlif_stop)
}

jlif_result pva_parse_integer(jlink *pjlink, long num)
{
    TRY{
        std::cerr<<"INT parse\n";
        return jlif_stop;
    }CATCH(jlif_stop)
}

jlif_result pva_parse_double(jlink *pjlink, double num)
{
    TRY{
        std::cerr<<"DOUBLE parse\n";
        return jlif_stop;
    }CATCH(jlif_stop)
}

jlif_result pva_parse_string(jlink *pjlink, const char *val, size_t len)
{
    TRY{
        if(pvt->parse_level==0) {
            std::string lstr(val, len);

            size_t A, B;
            A = lstr.find_first_not_of(" \t");
            B = lstr.find_first_of(" \t", A);

            if(A==lstr.npos || A==B) {
                std::cerr<<"Empty PVA target?\n";
                return jlif_stop;
            }

            pvt->name = lstr.substr(A, B==lstr.npos ? lstr.npos : B-A);

            for(A = lstr.find_first_not_of(" \t", B),
                B = lstr.find_first_of(" \t", A);
                A!=lstr.npos;
                A = lstr.find_first_not_of(" \t", B),
                B = lstr.find_first_of(" \t", A)
            ){
                size_t C = B==lstr.npos ? lstr.npos : B-A;

                if(lstr.compare(A, C, "CPP")==0)
                    pvt->linkmods |= pvlOptCPP;
                else if(lstr.compare(A, C, "CP")==0)
                    pvt->linkmods |= pvlOptCP;
                else if(lstr.compare(A, C, "MSI")==0)
                    pvt->linkmods |= pvlOptMSI;
                else if(lstr.compare(A, C, "MSS")==0)
                    pvt->linkmods |= pvlOptMSS;
                else if(lstr.compare(A, C, "MS")==0)
                    pvt->linkmods |= pvlOptMS;
                // else // unknown modifier?
            }

            std::cerr<<"Link set PVA name '"<<pvt->name<<"'\n";
            return jlif_continue;
        }
        std::cerr<<"STRING parse\n";
        return jlif_stop;
    }CATCH(jlif_stop)
}

jlif_key_result pva_parse_start_map(jlink *pjlink)
{
    TRY{
        std::cerr<<"{ parse\n";
        return jlif_key_stop;
    }CATCH(jlif_key_stop)
}

jlif_result pva_parse_map_key(jlink *pjlink, const char *key, size_t len)
{
    TRY{
        std::cerr<<"KEY parse\n";
        return jlif_stop;
    }CATCH(jlif_stop)
}

jlif_result pva_parse_end_map(jlink *pjlink)
{
    TRY{
        std::cerr<<"} parse\n";
        return jlif_stop;
    }CATCH(jlif_stop)
}

jlif_result pva_parse_start_array(jlink *pjlink)
{
    TRY{
        std::cerr<<"[ parse\n";
        return jlif_stop;
    }CATCH(jlif_stop)
}

jlif_result pva_parse_end_array(jlink *pjlink)
{
    TRY{
        std::cerr<<"] parse\n";
        return jlif_stop;
    }CATCH(jlif_stop)
}

struct lset* pva_get_lset(const jlink *pjlink)
{
    return &pva_lset;
}

void pva_report(const jlink *rpjlink, int lvl, int indent)
{
    jlink *pjlink = const_cast<jlink*>(rpjlink);
    TRY {
        const char * fname = "???", //TODO: how to find out?
                   * rname = pvt->plink->precord->name;
        int connected = pvt->lchan->chan && pvt->lchan->chanmon;

        if(connected) {

            if(lvl>=1){
                printf("%*s%28s.%-4s ==> pva://%s.%s\n",
                       indent, "", rname, fname,
                       pvt->name.c_str(), pvt->field.c_str());
            }
        } else {
            if(lvl>=0){
                printf("%*s%28s.%-4s --> pva://%s.%s\n",
                       indent, "", rname, fname,
                       pvt->name.c_str(), pvt->field.c_str());
            }
        }
    }CATCH()
}


jlif lsetPVA = {
    "pva",
    &pva_alloc_jlink,
    &pva_free_jlink,
    &pva_parse_null,
    &pva_parse_boolean,
    &pva_parse_integer,
    &pva_parse_double,
    &pva_parse_string,
    &pva_parse_start_map,
    &pva_parse_map_key,
    &pva_parse_end_map,
    &pva_parse_start_array,
    &pva_parse_end_array,
    &pva_end_child,
    &pva_get_lset,
    &pva_report,
    NULL
};

} // namespace

void pvalr(int level)
{
    try {
        std::cout<<"pvaLink        count "<<epics::atomic::get(pvaLink::refs)<<"\n"
                   "pvaLinkChannel count "<<epics::atomic::get(pvaLinkChannel::refs)<<"\n";
    }catch(std::exception& e){
        std::cerr<<"Error :"<<e.what()<<"\n";
    }
}

static
void installPVAAddLinkHook()
{
    initHookRegister(&initPVALink);
    iocshRegister<int, &pvalr>("pvalr", "level");
    iocshVariable<int, &pvaLinkDebug>("pvaLinkDebug");
}

epicsExportRegistrar(installPVAAddLinkHook);
epicsExportAddress(jlif, lsetPVA);
