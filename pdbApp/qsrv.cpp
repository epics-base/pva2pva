
#include <epicsExport.h>
#include <initHooks.h>
#include <epicsExit.h>
#include <epicsThread.h>

#include <dbAccess.h>
#include <dbChannel.h>
#include <dbStaticLib.h>
#include <dbLock.h>
#include <dbEvent.h>
#include <epicsVersion.h>

#include <pv/reftrack.h>
#include <pv/pvAccess.h>
#include <pv/serverContext.h>
#include <pv/iocshelper.h>

#define epicsExportSharedSymbols

#include "pvahelper.h"
#include "pvif.h"
#include "pdb.h"
#include "pdbsingle.h"
#ifdef USE_MULTILOCK
#  include "pdbgroup.h"
#endif

namespace pva = epics::pvAccess;

void QSRVRegistrar_counters()
{
    epics::registerRefCounter("PDBSinglePV", &PDBSinglePV::num_instances);
    epics::registerRefCounter("PDBSingleChannel", &PDBSingleChannel::num_instances);
    epics::registerRefCounter("PDBSinglePut", &PDBSinglePut::num_instances);
    epics::registerRefCounter("PDBSingleMonitor", &PDBSingleMonitor::num_instances);
#ifdef USE_MULTILOCK
    epics::registerRefCounter("PDBGroupPV", &PDBGroupPV::num_instances);
    epics::registerRefCounter("PDBGroupChannel", &PDBGroupChannel::num_instances);
    epics::registerRefCounter("PDBGroupPut", &PDBGroupPut::num_instances);
    epics::registerRefCounter("PDBGroupMonitor", &PDBGroupMonitor::num_instances);
#endif // USE_MULTILOCK
    epics::registerRefCounter("PDBProvider", &PDBProvider::num_instances);
}

static
void QSRVRegistrar()
{
    QSRVRegistrar_counters();
    pva::ChannelProviderRegistry::servers()->addSingleton<PDBProvider>("QSRV");
}

extern "C" {
    epicsExportRegistrar(QSRVRegistrar);
}
