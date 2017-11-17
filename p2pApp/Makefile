TOP=..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

INC += pva2pvaVersion.h
INC += pva2pvaVersionNum.h

PROD_HOST = p2p

p2p_SRCS += gwmain.cpp
p2p_LIBS += p2pcore pvAccessIOC pvAccess pvData Com

LIBRARY_HOST += p2pcore

p2pcore_SRCS += server.cpp
p2pcore_SRCS += chancache.cpp
p2pcore_SRCS += moncache.cpp
p2pcore_SRCS += channel.cpp

p2pcore_LIBS += pvAccess pvData Com

EXPANDVARS += EPICS_PVA2PVA_MAJOR_VERSION
EXPANDVARS += EPICS_PVA2PVA_MINOR_VERSION
EXPANDVARS += EPICS_PVA2PVA_MAINTENANCE_VERSION
EXPANDVARS += EPICS_PVA2PVA_DEVELOPMENT_FLAG

EXPANDFLAGS += $(foreach var,$(EXPANDVARS),-D$(var)="$(strip $($(var)))")

# shared library ABI version.
SHRLIB_VERSION = 1.0.0

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

# Can't use EXPAND as generated headers must appear
# in O.Common, but EXPAND emits rules for O.$(T_A)
../O.Common/pva2pvaVersionNum.h: ../pva2pvaVersionNum.h@
	$(EXPAND_TOOL) $(EXPANDFLAGS) $($@_EXPANDFLAGS) $< $@
