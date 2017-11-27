TOP=..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

INC += qsrvVersion.h
INC += qsrvVersionNum.h

LIBRARY += qsrv

USR_CPPFLAGS += -I$(TOP)/common -I$(TOP)/p2pApp

qsrv_SRCS += pvif.cpp
qsrv_SRCS += qsrv.cpp
qsrv_SRCS += pdb.cpp
qsrv_SRCS += pdbsingle.cpp
#qsrv_SRCS += pvalink.cpp
qsrv_SRCS += demo.cpp
qsrv_SRCS += imagedemo.c

ifdef BASE_3_16
qsrv_SRCS += pdbgroup.cpp
qsrv_SRCS += configparse.cpp
endif

qsrv_LIBS += pvAccess pvData
qsrv_LIBS += $(EPICS_BASE_IOC_LIBS)

FINAL_LOCATION ?= $(shell $(PERL) $(TOOLS)/fullPathName.pl $(INSTALL_LOCATION))

USR_CPPFLAGS += -DFINAL_LOCATION="\"$(FINAL_LOCATION)\""

PROD_IOC += softIocPVA

softIocPVA_SRCS += softMain.cpp
softIocPVA_SRCS += softIocPVA_registerRecordDeviceDriver.cpp

softIocPVA_LIBS += qsrv
softIocPVA_LIBS += $(EPICS_BASE_PVA_CORE_LIBS)
softIocPVA_LIBS += $(EPICS_BASE_IOC_LIBS)

DBD += softIocPVA.dbd
DBD += qsrv.dbd

softIocPVA_DBD += softIoc.dbd
softIocPVA_DBD += PVAServerRegister.dbd
softIocPVA_DBD += qsrv.dbd

EXPANDVARS += EPICS_QSRV_MAJOR_VERSION
EXPANDVARS += EPICS_QSRV_MINOR_VERSION
EXPANDVARS += EPICS_QSRV_MAINTENANCE_VERSION
EXPANDVARS += EPICS_QSRV_DEVELOPMENT_FLAG

EXPANDFLAGS += $(foreach var,$(EXPANDVARS),-D$(var)="$(strip $($(var)))")

# shared library ABI version.
SHRLIB_VERSION = 1.0.0

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

# Can't use EXPAND as generated headers must appear
# in O.Common, but EXPAND emits rules for O.$(T_A)
../O.Common/qsrvVersionNum.h: ../qsrvVersionNum.h@
	$(EXPAND_TOOL) $(EXPANDFLAGS) $($@_EXPANDFLAGS) $< $@
