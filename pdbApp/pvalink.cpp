
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
#include <epicsStdio.h> /* redirects stdout/stderr */

#include <pv/pvAccess.h>
#include <pv/clientFactory.h>
#include <pv/iocshelper.h>
#include <pv/reftrack.h>

#define epicsExportSharedSymbols
#include <shareLib.h>

#include "helper.h"
#include "pvif.h"
#include "pvalink.h"

#include <epicsExport.h>

int pvaLinkDebug;
int pvaLinkIsolate;

using namespace pvalink;

namespace {

static void stopPVAPool(void*)
{
    pvaGlobal->queue.close();
}

static void finalizePVA(void*)
{
    try {
        {
            Guard G(pvaGlobal->lock);
            if(pvaGlobal->channels.size()) {
                fprintf(stderr, "pvaLink leaves %zu channels open\n",
                        pvaGlobal->channels.size());
            }
        }

        delete pvaGlobal;
        pvaGlobal = NULL;

    }catch(std::exception& e){
        fprintf(stderr, "Error initializing pva link handling : %s\n", e.what());
    }
}

void initPVALink(initHookState state)
{
    if(state==initHookAfterCaLinkInit) {
        // before epicsExit(exitDatabase)
        // so hook registered here will be run after iocShutdown()
        // which closes links
        try {
            pvaGlobal = new pvaGlobal_t;

            epicsAtExit(finalizePVA, NULL);

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
}
