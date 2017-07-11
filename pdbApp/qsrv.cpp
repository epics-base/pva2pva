
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

static
void QSRVRegistrar()
{
    pva::ChannelProviderRegistry::servers()->add<PDBProvider>("QSRV");
}

epicsExportRegistrar(QSRVRegistrar);
