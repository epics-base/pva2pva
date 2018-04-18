
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
#include <link.h>
#include <dbJLink.h>
#include <epicsUnitTest.h>

#include <epicsStdio.h> /* redirects stdout/stderr */

#include <pv/pvAccess.h>
#include <pv/clientFactory.h>
#include <pv/iocshelper.h>
#include <pv/reftrack.h>

#define epicsExportSharedSymbols
#include <shareLib.h>

#include "pv/qsrv.h"
#include "helper.h"
#include "pvif.h"
#include "pvalink.h"

#include <epicsExport.h>

int pvaLinkDebug;
int pvaLinkIsolate;

using namespace pvalink;

namespace {

// halt, and clear, scan workers before dbCloseLinks()  (cf. iocShutdown())
static void shutdownStep1()
{
    // no locking here as we assume that shutdown doesn't race startup
    if(!pvaGlobal) return;

    pvaGlobal->queue.close();
}

// Cleanup pvaGlobal, including PVA client and QSRV providers ahead of PDB cleanup
// specifically QSRV provider must be free'd prior to db_cleanup_events()
static void shutdownStep2()
{
    if(!pvaGlobal) return;

    {
        Guard G(pvaGlobal->lock);
        if(pvaGlobal->channels.size()) {
            fprintf(stderr, "pvaLink leaves %zu channels open\n",
                    pvaGlobal->channels.size());
        }
    }

    delete pvaGlobal;
    pvaGlobal = NULL;
}

static void stopPVAPool(void*)
{
    try {
        shutdownStep1();
    }catch(std::exception& e){
        fprintf(stderr, "Error while stopping PVA link pool : %s\n", e.what());
    }
}

static void finalizePVA(void*)
{
    try {
        shutdownStep2();
    }catch(std::exception& e){
        fprintf(stderr, "Error initializing pva link handling : %s\n", e.what());
    }
}

bool atexitInstalled;

void initPVALink(initHookState state)
{
    if(state==initHookAfterCaLinkInit) {
        // before epicsExit(exitDatabase)
        // so hook registered here will be run after iocShutdown()
        // which closes links
        try {
            if(pvaGlobal) {
                cantProceed("# Missing call to testqsrvShutdownOk() and/or testqsrvCleanup()");
            }
            pvaGlobal = new pvaGlobal_t;

            if(!atexitInstalled) {
                epicsAtExit(finalizePVA, NULL);
                atexitInstalled = true;
            }

        }catch(std::exception& e){
            cantProceed("Error initializing pva link handling : %s\n", e.what());
        }

    } else if(state==initHookAfterIocBuilt) {
        // after epicsExit(exitDatabase)
        // so hook registered here will be run before iocShutdown()
        epicsAtExit(stopPVAPool, NULL);
    }
}

} // namespace

// halt, and clear, scan workers before dbCloseLinks()  (cf. iocShutdown())
void testqsrvShutdownOk(void)
{
    try {
        shutdownStep1();
    }catch(std::exception& e){
        testAbort("Error while stopping PVA link pool : %s\n", e.what());
    }
}

void testqsrvCleanup(void)
{
    try {
        shutdownStep2();
    }catch(std::exception& e){
        testAbort("Error initializing pva link handling : %s\n", e.what());
    }
}

static
void installPVAAddLinkHook()
{
    initHookRegister(&initPVALink);
    epics::registerRefCounter("pvaLinkChannel", &pvaLinkChannel::num_instances);
    epics::registerRefCounter("pvaLink", &pvaLink::num_instances);
}

extern "C" {
    epicsExportRegistrar(installPVAAddLinkHook);
    epicsExportAddress(jlif, lsetPVA);
    epicsExportAddress(int, pvaLinkDebug);
    epicsExportAddress(int, pvaLinkNWorkers);
}
