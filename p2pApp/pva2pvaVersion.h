/*************************************************************************\
* Copyright (c) 2017 UChicago Argonne LLC, as Operator of Argonne
*     National Laboratory.
* EPICS BASE is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

#ifndef PVA2PVAVERSION_H
#define PVA2PVAVERSION_H

#include <epicsVersion.h>
#include <shareLib.h>

#ifndef VERSION_INT
#  define VERSION_INT(V,R,M,P) ( ((V)<<24) | ((R)<<16) | ((M)<<8) | (P))
#endif

/* include generated headers with:
 *   EPICS_PVA2PVA_MAJOR_VERSION
 *   EPICS_PVA2PVA_MINOR_VERSION
 *   EPICS_PVA2PVA_MAINTENANCE_VERSION
 *   EPICS_PVA2PVA_DEVELOPMENT_FLAG
 */
#include "pva2pvaVersionNum.h"

#define PVA2PVA_VERSION_INT VERSION_INT(EPICS_PVA2PVA_MAJOR_VERSION, EPICS_PVA2PVA_MINOR_VERSION, EPICS_PVA2PVA_MAINTENANCE_VERSION, 0)

#endif // PVA2PVAVERSION_H
