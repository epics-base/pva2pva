/*************************************************************************\
* Copyright (c) 2020 Michael Davidsaver
* EPICS BASE is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

#include <dbUnitTest.h>
#include <testMain.h>

#include <errlog.h>
#include <dbAccess.h>
#include <waveformRecord.h>
#include <tableAggRecord.h>

#include <sstream>

static const size_t NELM = 5;

extern "C" {
void tableTestIoc_registerRecordDeviceDriver(struct dbBase *);
}

namespace {

namespace pvd = epics::pvData;

void testGetPut()
{
    // Get handles to record
    tableAggRecord *ptable = (tableAggRecord*)testdbRecordPtr("TABLE");

    DBADDR addr;

    if (!ptable || dbNameToAddr("TABLE", &addr))
        testAbort("Failed to find record TABLE");

    // Process table record
    testdbPutFieldOk("TABLE.PROC", DBF_LONG, 1);

    // Fetch Structure
    VSharedStructure sval;
    sval.vtype = &vfStructure;
    sval.value = new pvd::StructureConstPtr();

    testOk1(!dbGetField(&addr, DBR_VFIELD, &sval, 0, 0, 0));

    // Check that the names of the columns are being correctly set
    const pvd::StringArray & cols = (*sval.value)->getFieldT<pvd::Structure>("value")->getFieldNames();
    testOk1(cols.size()==3);
    testOk1(cols.at(0)=="col1");
    testOk1(cols.at(1)=="col2");
    testOk1(cols.at(2)=="col3");

    // Fetch PVStructure
    VSharedPVStructure val;
    val.vtype = &vfPVStructure;
    val.value = new pvd::PVStructurePtr((*sval.value)->build());
    val.changed = new pvd::BitSet();

    testOk1(!dbGetField(&addr, DBR_VFIELD, &val, 0, 0, 0));

    // Check that the labels are being correctly set
    pvd::PVStringArrayPtr labels_ptr = (*val.value)->getSubFieldT<pvd::PVStringArray>("labels");
    pvd::shared_vector<const std::string> labels = labels_ptr->view();
    testOk1(labels.size()==3);
    testOk1(labels.at(0)=="One");
    testOk1(labels.at(1)=="Two");
    testOk1(labels.at(2)=="Three");

    // Check values for first column
    pvd::PVScalarArrayPtr col1_ptr = (*val.value)->getSubFieldT<pvd::PVScalarArray>("value.col1");
    pvd::shared_vector<const double> col1;
    col1_ptr->getAs(col1);
    testOk1(col1.size()==3);
    testOk1(col1.at(0)==1);
    testOk1(col1.at(1)==2);
    testOk1(col1.at(2)==3);

    // Check values for second column
    pvd::PVScalarArrayPtr col2_ptr = (*val.value)->getSubFieldT<pvd::PVScalarArray>("value.col2");
    pvd::shared_vector<const pvd::int8> col2;
    col2_ptr->getAs(col2);
    testOk1(col2.size()==4);
    testOk1(col2.at(0)==4);
    testOk1(col2.at(1)==3);
    testOk1(col2.at(2)==2);
    testOk1(col2.at(3)==1);

    // Check values for third column
    pvd::PVScalarArrayPtr col3_ptr = (*val.value)->getSubFieldT<pvd::PVScalarArray>("value.col3");
    pvd::shared_vector<const pvd::int16> col3;
    col3_ptr->getAs(col3);
    testOk1(col3.size()==3);
    testOk1(col3.at(0)==7);
    testOk1(col3.at(1)==8);
    testOk1(col3.at(2)==9);
}

}

MAIN(testtable)
{
    testPlan(0);
    testdbPrepare();
    testdbReadDatabase("tableTestIoc.dbd", 0, 0);
    tableTestIoc_registerRecordDeviceDriver(pdbbase);

    std::stringstream macros;
    macros << "NELM=" << NELM;
    macros << ",FTVLA=DOUBLE,FNAA=col1,LABA=Column One";
    macros << ",FTVLB=DOUBLE,FNAB=col2,LABB=Column Two";
    macros << ",FTVLC=DOUBLE,FNAC=col3,LABC=Column Three";

    testdbReadDatabase("testtable.db", 0, macros.str().c_str());

    eltc(0);
    testIocInitOk();
    eltc(1);

    testGetPut();

    testIocShutdownOk();
    testdbCleanup();
    return testDone();
}
