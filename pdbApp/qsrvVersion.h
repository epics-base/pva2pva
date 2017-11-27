/*************************************************************************\
* Copyright (c) 2017 UChicago Argonne LLC, as Operator of Argonne
*     National Laboratory.
* EPICS BASE is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

#ifndef QSRVVERSION_H
#define QSRVVERSION_H

#include <epicsVersion.h>
#include <shareLib.h>

#ifndef VERSION_INT
#  define VERSION_INT(V,R,M,P) ( ((V)<<24) | ((R)<<16) | ((M)<<8) | (P))
#endif

/* include generated headers with:
 *   EPICS_QSRV_MAJOR_VERSION
 *   EPICS_QSRV_MINOR_VERSION
 *   EPICS_QSRV_MAINTENANCE_VERSION
 *   EPICS_QSRV_DEVELOPMENT_FLAG
 */
#include "qsrvVersionNum.h"

#define QSRV_VERSION_INT VERSION_INT(EPICS_QSRV_MAJOR_VERSION, EPICS_QSRV_MINOR_VERSION, EPICS_QSRV_MAINTENANCE_VERSION, 0)

#endif // QSRVVERSION_H
