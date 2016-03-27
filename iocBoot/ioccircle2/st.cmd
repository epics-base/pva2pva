#!../../bin/linux-x86_64-debug/softIocPVA

epicsEnvSet("EPICS_PVAS_INTF_ADDR_LIST","10.5.1.1")
epicsEnvSet("EPICS_PVA_ADDR_LIST", "10.5.1.255")
epicsEnvSet("EPICS_PVA_AUTO_ADDR_LIST","NO")

dbLoadRecords("circle2.db","")

iocInit()
