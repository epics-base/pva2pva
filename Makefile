# Makefile at top of application tree
TOP = .
include $(TOP)/configure/CONFIG

# Directories to build, any order
DIRS += configure
DIRS += p2pApp
ifeq ($(DEVEL),YES)
DIRS += pdbApp
endif
DIRS += testApp
#DIRS += $(wildcard iocBoot)

# iocBoot depends on all *App dirs
iocBoot_DEPEND_DIRS += $(filter %App,$(DIRS))
testApp_DEPEND_DIRS += p2pApp

# Add any additional dependency rules here:

include $(TOP)/configure/RULES_TOP
