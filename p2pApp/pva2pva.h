#ifndef PVA2PVA_H
#define PVA2PVA_H

#include <epicsGuard.h>

#include <pv/pvAccess.h>

#include <shareLib.h>

typedef epicsGuard<epicsMutex> Guard;
typedef epicsGuardRelease<epicsMutex> UnGuard;

epicsShareExtern void registerGWServerIocsh();
epicsShareExtern void registerGWClientIocsh();
epicsShareExtern void gwServerShutdown();
epicsShareExtern void gwClientShutdown();

#endif // PVA2PVA_H
