# Bind gateway server side to this interface
epicsEnvSet("EPICS_PVAS_INTF_ADDR_LIST","10.0.1.200")

# Gateway client side searches here.  Must not include gateway server side interface
epicsEnvSet("EPICS_PVA_ADDR_LIST", "10.1.1.255")
# Prevent gateway client from automatically populating the address list,
# which would include the gateway server side interface
epicsEnvSet("EPICS_PVA_AUTO_ADDR_LIST","NO")

gwstart()

# PVA variables
#
# Server side
#
# EPICS_PVAS_INTF_ADDR_LIST - Bind to this interface for both UDP and TCP
# EPICS_PVAS_SERVER_PORT - default TCP port
# EPICS_PVAS_BROADCAST_PORT - Listen for searches on this port
#
# EPICS_PVA_SERVER_PORT - Unused if EPICS_PVAS_SERVER_PORT set
#
# Client side
#
# EPICS_PVA_BROADCAST_PORT - Default search port for *ADDR_LIST
# EPICS_PVA_ADDR_LIST - Space seperated list of search endpoints (bcast or unicast)
# EPICS_PVA_AUTO_ADDR_LIST - YES/NO whether to populate ADDR_LIST with all local interface bcast addrs
