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

void testGetPut()
{
    // Get handles to records
    waveformRecord *pina = (waveformRecord*)testdbRecordPtr("INA");
    waveformRecord *pinb = (waveformRecord*)testdbRecordPtr("INB");
    waveformRecord *pinc = (waveformRecord*)testdbRecordPtr("INC");
    tableAggRecord *ptable = (tableAggRecord*)testdbRecordPtr("TABLE");

    DBADDR addra, addrb, addrc, addrtable;

    if (!pina || dbNameToAddr("INA", &addra))
        testAbort("Failed to find record INA");

    if (!pinb || dbNameToAddr("INB", &addrb))
        testAbort("Failed to find record INB");

    if (!pinc || dbNameToAddr("INC", &addrc))
        testAbort("Failed to find record INC");

    if (!ptable || dbNameToAddr("TABLE", &addrtable))
        testAbort("Failed to find record TABLE");

    // Generate test data
    double dataa[NELM], datab[NELM], datac[NELM];
    for (size_t i = 0; i < NELM; ++i) {
        dataa[i] = (double)i;
        datab[i] = (double)(NELM - i - 1);
        datac[i] = (dataa[i] + datab[i]) / 2.0;
    }

    // Put test data into inputs
    dbScanLock((dbCommon*)pina);
    dbPut(&addra, DBF_DOUBLE, &dataa, NELM);
    dbScanUnlock((dbCommon*)pina);

    dbScanLock((dbCommon*)pinb);
    dbPut(&addrb, DBF_DOUBLE, &datab, NELM);
    dbScanUnlock((dbCommon*)pinb);

    dbScanLock((dbCommon*)pinc);
    dbPut(&addrc, DBF_DOUBLE, &datac, NELM);
    dbScanUnlock((dbCommon*)pinc);

    // Process table record
    testdbPutFieldOk("TABLE.PROC", DBF_LONG, 1);



    /*VString sval;
    sval.vtype = &vfStdString;
    sval.value = "This is a long test value to ensure std::string allocates";

    testdbPutFieldOk("recsrc", DBR_VFIELD, &sval);

    dbScanLock((dbCommon*)psrc);
    testOk1(psrc->val==sval.value);
    dbScanUnlock((dbCommon*)psrc);

    VString sval2;
    sval2.vtype = &vfStdString;

    testOk1(!dbGetField(&addr, DBR_VFIELD, &sval2, 0, 0, 0));
    testOk1(sval2.value==sval.value);

    testdbPutFieldOk("recdst.PROC", DBF_LONG, 1);

    dbScanLock((dbCommon*)pdst);
    testOk1(pdst->val==sval.value);
    dbScanUnlock((dbCommon*)pdst);

    testdbGetArrFieldEqual("recsrc", DBF_CHAR, sval.value.size()+1, sval.value.size()+1, sval.value.c_str());*/
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
