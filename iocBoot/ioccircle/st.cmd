#!../../bin/linux-x86_64-debug/softIocPVA

epicsEnvSet("EPICS_PVA_ADDR_LIST", "10.5.2.255")
epicsEnvSet("EPICS_PVAS_INTF_ADDR_LIST","10.5.2.1")
epicsEnvSet("EPICS_PVA_AUTO_ADDR_LIST","NO")

dbLoadRecords("circle.db","")

iocInit()
