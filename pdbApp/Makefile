TOP=..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

LIBRARY_HOST += pdbcore

USR_CPPFLAGS += -I$(TOP)/common -I$(TOP)/p2pApp

pdbcore_SRCS += pvif.cpp
pdbcore_SRCS += qsrv.cpp
pdbcore_SRCS += pdb.cpp
pdbcore_SRCS += pdbsingle.cpp
pdbcore_SRCS += pdbgroup.cpp
pdbcore_SRCS += pvalink.cpp

pdbcore_LIBS += pvAccess pvData Com

FINAL_LOCATION ?= $(shell $(PERL) $(TOOLS)/fullPathName.pl $(INSTALL_LOCATION))

USR_CPPFLAGS += -DFINAL_LOCATION="\"$(FINAL_LOCATION)\""

PROD_IOC += softIocPVA

softIocPVA_SRCS += softMain.cpp
softIocPVA_SRCS += softIocPVA_registerRecordDeviceDriver.cpp

softIocPVA_LIBS += pdbcore pvAccess pvData $(MBLIB)
softIocPVA_LIBS += $(EPICS_BASE_IOC_LIBS)

DBD += softIocPVA.dbd
DBD += qsrv.dbd

softIocPVA_DBD += softIoc.dbd
softIocPVA_DBD += qsrv.dbd

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

