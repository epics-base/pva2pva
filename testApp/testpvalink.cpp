
#include <dbUnitTest.h>
#include <testMain.h>

#include "utilities.h"
#include "pvalink.h"

namespace {

void testGet()
{
    testDiag("==== testGet ====");

    testdbGetFieldEqual("target:li.VAL", DBF_LONG, 42);
    testdbGetFieldEqual("src:li1.VAL", DBF_LONG, 0);
    testdbGetFieldEqual("src:li1.INP", DBF_STRING, "{\"pva\":\"target:li\"}");

    testdbPutFieldOk("src:li1.PROC", DBF_LONG, 1);
    //TODO: wait for dbEvent queue update
    epicsThreadSleep(0.1);

    testdbGetFieldEqual("src:li1.VAL", DBF_LONG, 42);

    testdbPutFieldOk("src:li1.INP", DBF_STRING, "{\"pva\":\"target:ai\"}");

    testdbGetFieldEqual("src:li1.VAL", DBF_LONG, 42);

    //TODO: wait for pvalink worker update
    epicsThreadSleep(0.1);
    testdbPutFieldOk("src:li1.PROC", DBF_LONG, 1);
    //TODO: wait for dbEvent queue update
    epicsThreadSleep(0.1);

    testdbGetFieldEqual("src:li1.VAL", DBF_LONG, 4);
}

void testPut()
{
    testDiag("==== testPut ====");
    testdbGetFieldEqual("target:li2.VAL", DBF_LONG, 43);
    testdbGetFieldEqual("src:li2.VAL", DBF_LONG, 0);
    testdbGetFieldEqual("src:li2.OUT", DBF_STRING, "{\"pva\":\"target:li2\"}");

    testdbPutFieldOk("src:li2.VAL", DBF_LONG, 14);

    testdbGetFieldEqual("target:li2.VAL", DBF_LONG, 14);
    testdbGetFieldEqual("src:li2.VAL", DBF_LONG, 14);
}

} // namespace

extern "C"
void pvaLinkTestIoc_registerRecordDeviceDriver(struct dbBase *);

MAIN(testpvalink)
{
    testPlan(15);

    // Disable PVA client provider, use local/QSRV provider
    pvaLinkIsolate = 1;
    pvaLinkDebug = 5;

    try {
        TestIOC IOC;

        testdbReadDatabase("pvaLinkTestIoc.dbd", NULL, NULL);
        pvaLinkTestIoc_registerRecordDeviceDriver(pdbbase);
        testdbReadDatabase("testpvalink.db", NULL, NULL);

        IOC.init();
        testGet();
        testPut();
        IOC.shutdown();

    }catch(std::exception& e){
        testFail("Unexpected exception: %s", e.what());
    }
    // call epics atexits explicitly as workaround for c++ static dtor issues...
    epicsExit(testDone());
}
