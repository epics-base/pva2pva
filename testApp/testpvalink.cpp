
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

    longinRecord *li1 = (longinRecord*)testdbRecordPtr("src:li1");

    while(!dbIsLinkConnected(&li1->inp))
        testqsrvWaitForLinkEvent(&li1->inp);

    testdbGetFieldEqual("target:li.VAL", DBF_LONG, 42);

    testdbGetFieldEqual("src:li1.VAL", DBF_LONG, 0); // value before first process

    testdbGetFieldEqual("src:li1.INP", DBF_STRING, "{\"pva\":\"target:li\"}");

    testdbPutFieldOk("src:li1.PROC", DBF_LONG, 1);

    testdbGetFieldEqual("src:li1.VAL", DBF_LONG, 42);

    testdbPutFieldOk("src:li1.INP", DBF_STRING, "{\"pva\":\"target:ai\"}");

    while(!dbIsLinkConnected(&li1->inp))
        testqsrvWaitForLinkEvent(&li1->inp);

    testdbGetFieldEqual("src:li1.VAL", DBF_LONG, 42); // changing link doesn't automatically process

    testdbPutFieldOk("src:li1.PROC", DBF_LONG, 1);

    testdbGetFieldEqual("src:li1.VAL", DBF_LONG, 4); // now it's changed
}

void testPut()
{
    testDiag("==== testPut ====");

    longoutRecord *lo2 = (longoutRecord*)testdbRecordPtr("src:lo2");

    while(!dbIsLinkConnected(&lo2->out))
        testqsrvWaitForLinkEvent(&lo2->out);

    testdbGetFieldEqual("target:li2.VAL", DBF_LONG, 43);
    testdbGetFieldEqual("src:lo2.VAL", DBF_LONG, 0);
    testdbGetFieldEqual("src:lo2.OUT", DBF_STRING, "{\"pva\":\"target:li2\"}");

    testdbPutFieldOk("src:lo2.VAL", DBF_LONG, 14);

    testdbGetFieldEqual("target:li2.VAL", DBF_LONG, 14);
    testdbGetFieldEqual("src:lo2.VAL", DBF_LONG, 14);
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
