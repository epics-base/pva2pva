# Makefile at top of application tree
TOP = .
include $(TOP)/configure/CONFIG

# Directories to build, any order
DIRS += configure
DIRS += pdbApp
ifeq ($(BUILD_P2P),YES)
DIRS += p2pApp
endif
DIRS += iocBoot

p2pApp_DEPEND_DIRS += configure
pdbApp_DEPEND_DIRS += configure

iocBoot_DEPEND_DIRS += $(filter %App,$(DIRS))

testApp_DEPEND_DIRS += p2pApp pdbApp

# Add any additional dependency rules here:

include $(TOP)/configure/RULES_TOP
