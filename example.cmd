# Bind gateway server side to this interface
epicsEnvSet("EPICS_PVAS_INTF_ADDR_LIST","10.0.1.200")
# Gateway client side searches here.  Must not include gateway server side interface
epicsEnvSet("EPICS_PVA_ADDR_LIST", "10.1.1.255")
# Prevent gateway client from automatically populating the address list,
# which would include the gateway server side interface
epicsEnvSet("EPICS_PVA_AUTO_ADDR_LIST","NO")

gwstart()
