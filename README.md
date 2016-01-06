PV Access to PV Access protocol gateway
=======================================

The is unreleased, untested, alpha level software.
You have been warned.

Dependencies
------------

- [epics-base](http://www.aps.anl.gov/epics/) >= 3.15.3
- [pvDataCPP](http://epics-pvdata.sourceforge.net/)
- [pvAccessCPP](http://epics-pvdata.sourceforge.net/)

Portability
-----------

To this point all development/testing has been been carried out on Debian Linux 8 on amd64.

Building
--------

At present pva2pva depends on un-merged changes to pvDataCPP and pvAccessCPP
It must be built against the source for development Git repositories.

```
git clone --recurse-submodules --branch pva2pva https://github.com/mdavidsaver/v4workspace.git
cd v4workspace
make -C epics-base
make -C pvData
make -C pvAccess
make -C pva2pva
```

Running
-------

Use of pva2pva requires a computer with at least two ethernet interfaces.
At present each pva2pva process can act as a uni-directional proxy,
presenting a pvAccess server on one interface,
and a client on other(s).

The file [example.cmd](example.cmd) provides a starting point.
Adjust *EPICS_PVAS_INTF_ADDR_LIST* and *EPICS_PVA_ADDR_LIST*
according to the host computer's network configuration.

At present there are no safe guard against creating loops
where a gateway client side connects to its own server side.
To avoid this ensure that the address list does not contain
the interface used for the server (either directly, or included in a broadcast domain).
*EPICS_PVA_AUTO_ADDR_LIST* __must__ remain set to *NO*.

```
cd pva2pva
./bin/linux-x86_64/pva2pva example.cmd
```
