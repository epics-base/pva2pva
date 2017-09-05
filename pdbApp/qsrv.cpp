
#include <epicsExport.h>
#include <initHooks.h>
#include <epicsExit.h>
#include <epicsThread.h>

#include <pv/reftrack.h>
#include <pv/pvAccess.h>
#include <pv/serverContext.h>

#include "pvahelper.h"
#include "iocshelper.h"
#include "pdb.h"
#include "pdbsingle.h"

namespace pva = epics::pvAccess;

static
void QSRVRegistrar()
{
    epics::registerRefCounter("PDBSinglePV", &PDBSinglePV::num_instances);
    epics::registerRefCounter("PDBSingleChannel", &PDBSingleChannel::num_instances);
    epics::registerRefCounter("PDBSinglePut", &PDBSinglePut::num_instances);
    epics::registerRefCounter("PDBSingleMonitor", &PDBSingleMonitor::num_instances);
    epics::registerRefCounter("PDBProvider", &PDBProvider::num_instances);
    pva::ChannelProviderRegistry::servers()->add<PDBProvider>("QSRV");
}

epicsExportRegistrar(QSRVRegistrar);
