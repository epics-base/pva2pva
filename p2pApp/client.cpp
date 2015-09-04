
#include <epicsGuard.h>

#include <pv/sharedPtr.h>
#include <pv/pvAccess.h>
#include <pv/clientFactory.h>

#define epicsExportSharedSymbols
#include "pva2pva.h"
#include "chancache.h"

namespace pvd = epics::pvData;
namespace pva = epics::pvAccess;


void registerGWClientIocsh()
{
    pva::ClientFactory::start();

}

void gwClientShutdown()
{
    pva::ClientFactory::stop();
}
