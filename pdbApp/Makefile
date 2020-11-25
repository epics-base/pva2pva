TOP=..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

ifeq ($(OS_CLASS),RTEMS)
# set by Base in configure/os/CONFIG.UnixCommon.Common
# then clobbered by included RTEMS make/host.cfg
# to drop the '-p'
MKDIR = mkdir -p
endif

LIBRARY += qsrv

SHRLIB_VERSION ?= $(EPICS_QSRV_ABI_MAJOR_VERSION).$(EPICS_QSRV_ABI_MINOR_VERSION)

USR_CPPFLAGS += -DQSRV_API_BUILDING
USR_CPPFLAGS += -I$(TOP)/common -I$(TOP)/p2pApp

INC += pv/qsrv.h
INC += pv/qsrvVersionNum.h

DBD += menuMultiArrayLayout.dbd

DBDINC += svectorinRecord
DBDINC += svectoroutRecord
DBDINC += pvstructinRecord
DBDINC += columnarinRecord
DBDINC += columnaroutRecord
DBDINC += statBinRecord
DBDINC += ndainRecord
DBDINC += multiArrayCommon

EXPANDVARS += EPICS_QSRV_MAJOR_VERSION
EXPANDVARS += EPICS_QSRV_MINOR_VERSION
EXPANDVARS += EPICS_QSRV_MAINTENANCE_VERSION
EXPANDVARS += EPICS_QSRV_DEVELOPMENT_FLAG
EXPANDVARS += EPICS_QSRV_ABI_MAJOR_VERSION
EXPANDVARS += EPICS_QSRV_ABI_MINOR_VERSION

EXPANDFLAGS += $(foreach var,$(EXPANDVARS),-D$(var)="$(strip $($(var)))")

qsrv_SRCS += pvif.cpp
qsrv_SRCS += qsrv.cpp
qsrv_SRCS += pdb.cpp
qsrv_SRCS += pdbsingle.cpp
qsrv_SRCS += demo.cpp
qsrv_SRCS += imagedemo.c

ifdef BASE_3_16
qsrv_SRCS += pdbgroup.cpp
qsrv_SRCS += configparse.cpp

qsrv_SRCS += dbf_copy.cpp
qsrv_SRCS += tpool.cpp

qsrv_SRCS += pvalink.cpp
qsrv_SRCS += pvalink_lset.cpp
qsrv_SRCS += pvalink_jlif.cpp
qsrv_SRCS += pvalink_link.cpp
qsrv_SRCS += pvalink_channel.cpp
else

qsrv_SRCS += pvalink_null.cpp
endif

qsrv_SRCS += vfield.cpp
qsrv_SRCS += svectorinRecord.cpp
qsrv_SRCS += svectoroutRecord.cpp
qsrv_SRCS += devSVInSoft.cpp
qsrv_SRCS += devSVOutSoft.cpp

qsrv_SRCS += pvstructinRecord.cpp
qsrv_SRCS += devPVSInSoft.cpp

qsrv_SRCS += columnarinRecord.cpp
qsrv_SRCS += columnaroutRecord.cpp

qsrv_SRCS += statBinRecord.cpp

qsrv_SRCS += ndainRecord.cpp
qsrv_SRCS += multiArrayCommon.cpp
qsrv_SRCS += devNDAInSoft.cpp

qsrv_LIBS += pvAccess pvData
qsrv_LIBS += $(EPICS_BASE_IOC_LIBS)

FINAL_LOCATION ?= $(shell $(PERL) $(TOOLS)/fullPathName.pl $(INSTALL_LOCATION))

# since we copy softMain.cpp from Base, use the same
# EPICS_BASE macro for our location
USR_CPPFLAGS += -DEPICS_BASE="\"$(FINAL_LOCATION)\""

PROD_IOC += softIocPVA

softIocPVA_SRCS += softMain.cpp
softIocPVA_SRCS += softIocPVA_registerRecordDeviceDriver.cpp

softIocPVA_LIBS += qsrv
softIocPVA_LIBS += $(EPICS_BASE_PVA_CORE_LIBS)
softIocPVA_LIBS += $(EPICS_BASE_IOC_LIBS)

DBD += softIocPVA.dbd
DBD += qsrv.dbd

ifneq ($(FINAL_LOCATION),$(EPICS_BASE))
# Copy from Base if we won't be installed there
DB_INSTALLS += $(EPICS_BASE)/db/softIocExit.db
endif

softIocPVA_DBD += softIoc.dbd
softIocPVA_DBD += PVAServerRegister.dbd
softIocPVA_DBD += qsrv.dbd

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

# why is this not automatic!
$(COMMON_DIR)/softIocPVA.dbd: ../svectorinRecord.dbd
$(COMMON_DIR)/softIocPVA.dbd: ../svectoroutRecord.dbd
$(COMMON_DIR)/softIocPVA.dbd: ../pvstructinRecord.dbd
$(COMMON_DIR)/softIocPVA.dbd: ../columnarinRecord.dbd
$(COMMON_DIR)/softIocPVA.dbd: ../columnaroutRecord.dbd
$(COMMON_DIR)/softIocPVA.dbd: ../statBinRecord.dbd
$(COMMON_DIR)/softIocPVA.dbd: ../ndainRecord.dbd
$(COMMON_DIR)/softIocPVA.dbd: $(COMMON_DIR)/qsrv.dbd

# Can't use EXPAND as generated headers must appear
# in O.Common, but EXPAND emits rules for O.$(T_A)
../O.Common/pv/qsrvVersionNum.h: ../pv/qsrvVersionNum.h@
	$(MKDIR) $(COMMON_DIR)/pv
	$(EXPAND_TOOL) $(EXPANDFLAGS) $($@_EXPANDFLAGS) $< $@

qsrv$(DEP): ../O.Common/pv/qsrvVersionNum.h
pvalink$(DEP): ../O.Common/pv/qsrvVersionNum.h

ifdef BASE_3_16
QSRVDBD = ../qsrv-new.dbd
else
QSRVDBD = ../qsrv-old.dbd
endif

$(QSRVDBD) : ../svectorinRecord.dbd
$(QSRVDBD) : ../svectoroutRecord.dbd
$(QSRVDBD) : ../pvstructinRecord.dbd
$(QSRVDBD) : ../columnarinRecord.dbd
$(QSRVDBD) : ../statBinRecord.dbd
$(QSRVDBD) : ../ndainRecord.dbd

../O.Common/qsrv.dbd: $(QSRVDBD)
	$(CP) $< $@

qsrv.dbd$(DEP):
	@$(RM) $@
	@echo "$(COMMONDEP_TARGET): ../Makefile" > $@

../O.Common/softIocPVA.dbd: ../O.Common/qsrv.dbd


# cf. specials for dbCommon.h in modules/database/src/ioc/db/RULES

multiArrayCommon.h$(DEP): ../multiArrayCommonRecord.dbd ../multiArrayCommon.dbd
	@$(RM) $@
	@$(DBTORECORDTYPEH) -D $(DBDFLAGS) -o $(COMMONDEP_TARGET) $< > $@

$(COMMON_DIR)/multiArrayCommon.h: ../multiArrayCommonRecord.dbd ../multiArrayCommon.dbd
	@$(RM) $(notdir $@)
	$(DBTORECORDTYPEH) $(DBDFLAGS) -o $(notdir $@) $<
	@$(MV) $(notdir $@) $@
