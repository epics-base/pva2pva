TOP=..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

LIBRARY_HOST += pdbcore

USR_CPPFLAGS += -I$(TOP)/common -I$(TOP)/p2pApp

pdbcore_SRCS += pvif.cpp
pdbcore_SRCS += pdbsingle.cpp
pdbcore_SRCS += pdbgroup.cpp

pdbcore_LIBS += pvAccess pvData Com

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

