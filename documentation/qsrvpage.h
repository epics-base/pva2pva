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

@code
$ pvget grp:name
grp:name
structure 
    epics:nt/NTScalar:1.0 X
        double value 0
        alarm_t alarm INVALID DRIVER UDF
        time_t timeStamp <undefined> 0
...
    epics:nt/NTScalar:1.0 Y
        double value 0
        alarm_t alarm INVALID DRIVER UDF
        time_t timeStamp <undefined> 0
...
@endcode

@subsection qsrv_group_ref Group PV reference

@code
record(...) {
    info(Q:group, {
        "<group_name>":{
            +id:"some/NT:1.0",  # top level ID
            +meta:"FLD",        # map top level alarm/timeStamp
            +atomic:true,       # whether monitors default to multi-locking atomicity
            "<field.name>":{
                +type:"scalar", # controls how map VAL mapped onto <field.name>
                +channel:"VAL",
                +id:"some/NT:1.0",
                +trigger:"*",   # "*" or comma seperated list of <field.name>s
                +putorder:0,    # set for fields where put is allowed, processing done in increasing order
            }
        }
    })
}
@endcode

@subsubsection qsrv_group_map_types Field mapping types

@li "scalar" or ""
@li "plain"
@li "any"
@li "meta"
@li "proc"

The "scalar" mapping places an NTScalar or NTScalarArray as a sub-structure.

The "plain" mapping ignores all meta-data and places only the "value" as a field.
The "value" is equivalent to '.value' of the equivalent NTScalar/NTScalarArray as a field.

The "any" mapping places a variant union into which the "value" is placed.

The "meta" mapping ignores the "value" and places only the alarm and time
meta-data as sub-fields.
The special group level tag 'meta:""' allows these meta-data fields to be
placed in the top-level structure.

The "proc" mapping uses neither "value" nor meta-data.
Instead the target record is processed during a put.

@subsubsection qsrv_group_map_trig Field Update Triggers

The field triggers define how changes to the consitutent field
are translated into a subscription update to the group.

The most use of these are "" which means that changes to the field
are ignored, and do not result group update.
And "*" which results in a group update containing the most recent
values/meta-data of all fields.

It may be useful to specify a comma seperated list of field names
so that changes may partially update the group.

@subsection qsrv_stamp QSRV Timestamp Options

QSRV has the ability to perform certain transformations on the timestamp before transporting it.
The mechanism for configuring this is the "Q:time:tag" info() tag.

@subsubsection qsrv_stamp_nslsb Nano-seconds least significant bits

Setting "Q:time:tag" to a value of "nsec:lsb:#", where # is a number between 0 and 32,
will split the nanoseconds value stored in the associated record.
The least significant # bits are stored in the 'timeStamp.userTag' field.
While the remaining 32-# bits are stored in 'timeStamp.nanoseconds' (without shifting).

For example, in the following situation 16 bits are split off.
If the nanoseconds part of the record timestamp is 0x12345678,
then the PVD structure would include "timeStamp.nanoseconds=0x12300000"
and "timeStamp.userTag=0x45678".

@code
record(ai, "...") {
  info(Q:time:tag, "nsec:lsb:20")
}
@endcode

*/
