/**

@page release_notes Release Notes

Release 1.1.0 (UNRELEASED)
==========================

- Incompatible changes
 - Requires pvDataCPP >= 7.1.0-pre1
 - Requires pvAccessCPP >= 6.1.0-pre1
- Removals
 - Drop the broken ioccircle and ioccircle2 examples.
- Fixes
 - Fix QSRV monitor locking causing crash
 - Fix Windows DLL import/export errors
 - Correctly handle empty "scalar" case of NELM=1, NORD=0.
- Additions
 - QSRV implement channelList() (aka. 'pvlist') with list of record and group names.
 - @ref qsrv_link type (requires Base >= 3.16.1)

Release 1.0.0 (Dec 2017)
========================

Initial Release

*/
