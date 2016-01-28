TOP=..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

PROD_HOST = p2p

p2p_SRCS += main.cpp
p2p_LIBS += p2pcore pvAccess pvData Com

LIBRARY_HOST += p2pcore

p2pcore_SRCS += server.cpp
p2pcore_SRCS += client.cpp
p2pcore_SRCS += chancache.cpp
p2pcore_SRCS += moncache.cpp
p2pcore_SRCS += channel.cpp

p2pcore_LIBS += pvAccess pvData Com

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

