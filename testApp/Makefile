TOP=..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

USR_CPPFLAGS = -I$(TOP)/p2pApp

LIBRARY += testutils

testutils_SRCS += utilities.cpp
testutils_LIBS += pvAccess pvData Com

TESTPROD_HOST += testweak
testweak_SRCS = testweak.cpp
testweak_LIBS = Com
TESTS += testweak

TESTPROD_HOST += testtest
testtest_SRCS = testtest.cpp
testtest_LIBS = testutils p2pcore pvAccess pvData Com
TESTS += testtest

TESTPROD_HOST += testmon
testmon_SRCS = testmon.cpp
testmon_LIBS = testutils p2pcore pvAccess pvData Com
TESTS += testmon

TESTSCRIPTS_HOST += $(TESTS:%=%.t)

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

