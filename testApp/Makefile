TOP=..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

USR_CPPFLAGS = -I$(TOP)/p2pApp

TESTPROD_HOST += testweak
testweak_SRCS = testweak.cpp
testweak_LIBS = Com
TESTS += testweak

TESTSCRIPTS_HOST += $(TESTS:%=%.t)

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

