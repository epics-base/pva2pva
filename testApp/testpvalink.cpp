
#include <dbUnitTest.h>
#include <testMain.h>
#include <longinRecord.h>
#include <longoutRecord.h>

#include <pv/qsrv.h>
#include "utilities.h"
#include "pvalink.h"
#include "pv/qsrv.h"

namespace {

void testGet()
{
    testDiag("==== testGet ====");

    longinRecord *li1 = (longinRecord*)testdbRecordPtr("src:i1");

    while(!dbIsLinkConnected(&li1->inp))
        testqsrvWaitForLinkEvent(&li1->inp);

    testdbGetFieldEqual("target:i.VAL", DBF_INT64, 42);

    testdbGetFieldEqual("src:i1.VAL", DBF_INT64, 0); // value before first process

    testdbGetFieldEqual("src:i1.INP", DBF_STRING, "{\"pva\":\"target:i\"}");

    testdbPutFieldOk("src:i1.PROC", DBF_INT64, 1);

    testdbGetFieldEqual("src:i1.VAL", DBF_INT64, 42);

    testdbPutFieldOk("src:i1.INP", DBF_STRING, "{\"pva\":\"target:ai\"}");

    while(!dbIsLinkConnected(&li1->inp))
        testqsrvWaitForLinkEvent(&li1->inp);

    testdbGetFieldEqual("src:i1.VAL", DBF_INT64, 42); // changing link doesn't automatically process

    testdbPutFieldOk("src:i1.PROC", DBF_INT64, 1);

    testdbGetFieldEqual("src:i1.VAL", DBF_INT64, 4); // now it's changed
}

void testPut()
{
    testDiag("==== testPut ====");

    longoutRecord *lo2 = (longoutRecord*)testdbRecordPtr("src:lo2");

    while(!dbIsLinkConnected(&lo2->out))
        testqsrvWaitForLinkEvent(&lo2->out);

    testdbGetFieldEqual("target:i2.VAL", DBF_INT64, 43);
    testdbGetFieldEqual("src:lo2.VAL", DBF_INT64, 0);
    testdbGetFieldEqual("src:lo2.OUT", DBF_STRING, "{\"pva\":\"target:i2\"}");

    testdbPutFieldOk("src:lo2.VAL", DBF_INT64, 14);

    testdbGetFieldEqual("target:i2.VAL", DBF_INT64, 14);
    testdbGetFieldEqual("src:lo2.VAL", DBF_INT64, 14);
}

} // namespace

extern "C"
void pvaLinkTestIoc_registerRecordDeviceDriver(struct dbBase *);

MAIN(testpvalink)
{
    testPlan(15);

    // Disable PVA client provider, use local/QSRV provider
    pvaLinkIsolate = 1;

    try {
        TestIOC IOC;

        testdbReadDatabase("pvaLinkTestIoc.dbd", NULL, NULL);
        pvaLinkTestIoc_registerRecordDeviceDriver(pdbbase);
        testdbReadDatabase("testpvalink.db", NULL, NULL);

        IOC.init();
        testGet();
        testPut();
        testqsrvShutdownOk();
        IOC.shutdown();
        testqsrvCleanup();

    }catch(std::exception& e){
        testFail("Unexpected exception: %s", e.what());
    }
    // call epics atexits explicitly as workaround for c++ static dtor issues...
    epicsExit(testDone());
}
