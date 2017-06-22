
#include <epicsExport.h>
#include <initHooks.h>
#include <epicsExit.h>
#include <epicsThread.h>

#include <pv/pvAccess.h>
#include <pv/serverContext.h>

#include "pvahelper.h"
#include "iocshelper.h"
#include "pdb.h"

namespace pva = epics::pvAccess;

namespace {

struct qsrv_t {
    epicsMutex mutex;

    pva::ServerContext::shared_pointer server;

    pva::ChannelProviderFactory::shared_pointer provider;
} *qsrv;

} // namespace

void qsrvStart()
{
    try{
        if(!qsrv) {
            qsrv = new qsrv_t;
        }
        epicsGuard<epicsMutex> G(qsrv->mutex);
        if(qsrv->server) {
            std::cout<<"QSRV already started\n";
        } else {
            qsrv->provider.reset(new BaseChannelProviderFactory<PDBProvider>("QSRV"));
            pva::registerChannelProviderFactory(qsrv->provider);
            qsrv->server = pva::startPVAServer("QSRV", 0, true, false);
            if(!qsrv->server.unique())
                printf("Warning: new QSRV server instance includes reference loop\n");
        }
    }catch(std::exception& e){
        printf("Error: %s\n", e.what());
    }
}

void qsrvStop()
{
    try{
        epicsGuard<epicsMutex> G(qsrv->mutex);
        if(!qsrv->server) {
            std::cout<<"QSRV not running\n";
        } else {
            if(!qsrv->server.unique())
                printf("Warning: QSRV server leaks1\n");
            qsrv->server->destroy();
            if(!qsrv->server.unique())
                printf("Warning: QSRV server leaks2\n");
            qsrv->server.reset();
            pva::unregisterChannelProviderFactory(qsrv->provider);
            if(!qsrv->provider.unique())
                printf("Warning: QSRV provider leaks\n");
            qsrv->provider.reset();
        }
    }catch(std::exception& e){
        printf("Error: %s\n", e.what());
    }
}

static
void QSRVExit(void *)
{
    qsrvStop();
    delete qsrv;
    qsrv = NULL;
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
    initHookRegister(QSRVHooks);
    iocshRegister<&qsrvStart>("qsrvStart");
    iocshRegister<&qsrvStop>("qsrvStop");
}

epicsExportRegistrar(QSRVRegistrar);
