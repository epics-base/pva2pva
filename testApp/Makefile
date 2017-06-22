TOP=..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

USR_CPPFLAGS += -I$(TOP)/p2pApp
USR_CPPFLAGS += -I$(TOP)/pdbApp
USR_CPPFLAGS += -I$(TOP)/common

TARGETS += $(COMMON_DIR)/p2pTestIoc.dbd
p2pTestIoc_DBD += base.dbd
# Tests explicitly create/destory PDB provider
#p2pTestIoc_DBD += qsrv.dbd

LIBRARY += testutils

testutils_SRCS += utilities.cpp
testutils_LIBS += pvAccess pvData Com

TESTPROD_HOST += testweak
testweak_SRCS = testweak.cpp
testweak_LIBS = Com
TESTS += testweak

TESTPROD_HOST += teststream
teststream_SRCS = teststream.cpp
teststream_LIBS = Com
TESTS += teststream

TESTPROD_HOST += testtest
testtest_SRCS = testtest.cpp
testtest_LIBS = testutils p2pcore pvAccess pvData Com
TESTS += testtest

TESTPROD_HOST += testmon
testmon_SRCS = testmon.cpp
testmon_LIBS = testutils p2pcore pvAccess pvData Com
TESTS += testmon


TESTPROD_HOST += testpvif
testpvif_SRCS += testpvif.cpp
testpvif_SRCS += p2pTestIoc_registerRecordDeviceDriver.cpp
testpvif_LIBS += pdbcore pvAccess pvData $(EPICS_BASE_IOC_LIBS)
TESTS += testpvif

TESTPROD_HOST += testpdb
testpdb_SRCS += testpdb.cpp
testpdb_SRCS += p2pTestIoc_registerRecordDeviceDriver.cpp
testpdb_LIBS += testutils pdbcore pvAccess pvData $(EPICS_BASE_IOC_LIBS)
TESTS += testpdb

PROD_HOST += check_consist
check_consist_SRCS += check_consist.cpp
check_consist_LIBS += testutils pvAccess pvData Com

#TESTPROD_HOST += testpvalink
testpvalink_SRCS += testpvalink.cpp
testpvalink_SRCS += p2pTestIoc_registerRecordDeviceDriver.cpp
testpvalink_LIBS += testutils pdbcore pvAccess pvData $(EPICS_BASE_IOC_LIBS)
#TESTS += testpvalink

TESTSCRIPTS_HOST += $(TESTS:%=%.t)


#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

