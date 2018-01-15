/**
@page qsrv_page QSRV

@section qsrv_config QSRV Configuration

By default QSRV exposes all Process Variables (fields of process database records).
In addition to these "single" PVs are special "group" PVs.

@subsection qsrv_single Single PVs

"single" PVs are the same set of names server by the Channel Access server (RSRV).
This is all accessible record fields.
So all data which is accessible via Channel Access is also accessible via PVAccess.

QSRV presents all "single" PVs as Structures conforming to the
Normative Types NTScalar, NTScalarArray, or NTEnum depending on the native DBF field type.

@subsection qsrv_group_def Group PV definitions

A group is defined using a JSON syntax.
Groups are defined with respect to a Group Name,
which is also the PV name.
So unlike records, the "field" of a group have a different meaning.
Group field names are _not_ part of the PV name.

A group definition is split among several records.
For example of a group including two records is:

@code
record(ai, "rec:X") {
  info(Q:group, {
    "grp:name": {
        "X": {+channel:"VAL"}
    }
  })
}
record(ai, "rec:Y") {
  info(Q:group, {
    "grp:name": {
        "Y": {+channel:"VAL"}
    }
  })
}
@endcode

This group, named "grp:name", has two fields "X" and "Y".

@subsection qsrv_group_sym Group PV semantics

*/
