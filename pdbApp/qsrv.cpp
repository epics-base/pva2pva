
#include <epicsExport.h>
#include <initHooks.h>
#include <epicsExit.h>

#include <pv/pvAccess.h>
#include <pv/serverContext.h>

#include "pvahelper.h"
#include "iocshelper.h"
#include "pdb.h"

namespace pva = epics::pvAccess;

static
epicsMutex qsrv_lock;

static
pva::ServerContext::shared_pointer qsrv;

void qsrvStart()
{
    try{
        epicsGuard<epicsMutex> G(qsrv_lock);
        if(qsrv) {
            std::cout<<"QSRV already started\n";
        } else {
            qsrv = pva::startPVAServer("QSRV", 0, true, false);
        }
    }catch(std::exception& e){
        printf("Error: %s\n", e.what());
    }
}

void qsrvStop()
{
    try{
        epicsGuard<epicsMutex> G(qsrv_lock);
        if(!qsrv) {
            std::cout<<"QSRV not running\n";
        } else {
            qsrv->destroy();
            qsrv.reset();
        }
    }catch(std::exception& e){
        printf("Error: %s\n", e.what());
    }
}

static
void QSRVExit(void *)
{
    qsrvStop();
}

static
void QSRVHooks(initHookState state)
{
    if(state!=initHookAfterCaServerInit)
        return;
    epicsAtExit(QSRVExit, NULL);
    qsrvStart();
}

static
void QSRVRegistrar()
{
    pva::ChannelProviderFactory::shared_pointer fact(new BaseChannelProviderFactory<PDBProvider>("QSRV"));
    initHookRegister(QSRVHooks);
    pva::registerChannelProviderFactory(fact);
    iocshRegister<&qsrvStart>("qsrvStart");
    iocshRegister<&qsrvStop>("qsrvStop");
}

epicsExportRegistrar(QSRVRegistrar);
