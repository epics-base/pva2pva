#ifndef PV_QSRV_H
#define PV_QSRV_H

#include <epicsVersion.h>
#include <shareLib.h>

#ifndef VERSION_INT
#  define VERSION_INT(V,R,M,P) ( ((V)<<24) | ((R)<<16) | ((M)<<8) | (P))
#endif

/* generated header with EPICS_QSRV_*_VERSION macros */
#  include <pv/qsrvVersionNum.h>

#define QSRV_VERSION_INT VERSION_INT(EPICS_QSRV_MAJOR_VERSION, EPICS_QSRV_MINOR_VERSION, EPICS_QSRV_MAINTENANCE_VERSION, !(EPICS_QSRV_DEVELOPMENT_FLAG))

#define QSRV_ABI_VERSION_INT VERSION_INT(EPICS_QSRV_ABI_MAJOR_VERSION, EPICS_QSRV_ABI_MINOR_VERSION, 0, 0)

#ifdef __cplusplus
extern "C" {
#endif

struct link; /* aka. DBLINK from link.h */

/** returns QSRV_VERSION_INT captured at compilation time */
epicsShareExtern unsigned qsrvVersion(void);

/** returns QSRV_ABI_VERSION_INT captured at compilation time */
epicsShareExtern unsigned qsrvABIVersion(void);

epicsShareFunc void testqsrvWaitForLinkEvent(struct link *plink);

/** Call before testIocShutdownOk()
 @code
   testdbPrepare();
   ...
   testIocInitOk();
   ...
   testqsrvShutdownOk();
   testIocShutdownOk();
   testqsrvCleanup();
   testdbCleanup();
 @endcode
 */
epicsShareExtern void testqsrvShutdownOk(void);

/** Call after testIocShutdownOk() and before testdbCleanup()
 @code
   testdbPrepare();
   ...
   testIocInitOk();
   ...
   testqsrvShutdownOk();
   testIocShutdownOk();
   testqsrvCleanup();
   testdbCleanup();
 @endcode
 */
epicsShareExtern void testqsrvCleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* PV_QSRV_H */
