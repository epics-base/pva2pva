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

template<typename T>
void testVector(const pvd::PVStructurePtr & value, const std::string & fname, size_t n, const T* expected) {
    pvd::PVScalarArrayPtr ptr = value->getSubFieldT<pvd::PVScalarArray>(fname);
    pvd::shared_vector<const T> actual;
    pvd::shared_vector<T> exp;

    exp.reserve(n);
    for (size_t i = 0; i < n; ++i)
        exp.push_back(expected[i]);

    ptr->getAs(actual);

    std::stringstream ss;
    ss << fname << ": " << actual << " == " << exp;

    testOk(actual == exp, "%s", ss.str().c_str());
}

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
    std::vector<std::string> exp_cols;
    exp_cols.push_back("col1");
    exp_cols.push_back("col2");
    exp_cols.push_back("col3");
    exp_cols.push_back("col4");
    testOk1(cols==exp_cols);

    // Fetch PVStructure
    VSharedPVStructure val;
    val.vtype = &vfPVStructure;
    val.value = new pvd::PVStructurePtr((*sval.value)->build());
    val.changed = new pvd::BitSet();

    testOk1(!dbGetField(&addr, DBR_VFIELD, &val, 0, 0, 0));

    // Check that the labels are being correctly set
    const std::string labels[] = {"One", "Two", "Three", "Four"};
    testVector(*val.value, "labels", NELEMENTS(labels), labels);

    // Check column values
    const double col1[] = {1, 2, 3};
    testVector(*val.value, "value.col1", NELEMENTS(col1), col1);

    const pvd::int32 col2[] = {4, 3, 2, 1};
    testVector(*val.value, "value.col2", NELEMENTS(col2), col2);

    const pvd::int16 col3[] = {7, 8, 9};
    testVector(*val.value, "value.col3", NELEMENTS(col3), col3);

    const std::string col4[] = {"First", "Second"};
    testVector(*val.value, "value.col4", NELEMENTS(col4), col4);
}

}

MAIN(testtable)
{
    testPlan(9);
    testdbPrepare();
    testdbReadDatabase("tableTestIoc.dbd", 0, 0);
    tableTestIoc_registerRecordDeviceDriver(pdbbase);

    testdbReadDatabase("testtable.db", 0, 0);

    eltc(0);
    testIocInitOk();
    eltc(1);

    testGetPut();

    testIocShutdownOk();
    testdbCleanup();
    return testDone();
}
