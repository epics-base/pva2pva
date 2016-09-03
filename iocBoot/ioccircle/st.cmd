#!../../bin/linux-x86_64-debug/softIocPVA

#epicsEnvSet("EPICS_PVA_ADDR_LIST", "10.5.2.255")
#epicsEnvSet("EPICS_PVAS_INTF_ADDR_LIST","10.5.2.1")
#epicsEnvSet("EPICS_PVA_AUTO_ADDR_LIST","NO")

#epicsEnvSet("EPICS_PVA_SERVER_PORT","5085")
#epicsEnvSet("EPICS_PVA_BROADCAST_PORT","5086")
#epicsEnvSet("EPICS_CA_SERVER_PORT","5067")
#epicsEnvSet("EPICS_CA_REPEATER_PORT","5068")
#epicsEnvSet("EPICS_CA_MAX_ARRAY_BYTES","13000000")

dbLoadRecords("circle.db","")

iocInit()
