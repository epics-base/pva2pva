
#include <dbUnitTest.h>
#include <testMain.h>

#include "utilities.h"
#include "pvalink.h"

namespace {

void testGet()
{
    testdbGetFieldEqual("target:li.VAL", DBF_LONG, 42);
    testdbGetFieldEqual("src:li1.VAL", DBF_LONG, 0);
    testdbGetFieldEqual("src:li1.INP", DBF_STRING, "{\"pva\":\"target:li\"}");

    testdbPutFieldOk("src:li1.PROC", DBF_LONG, 1);
    //TODO: wait for dbEvent queue update
    epicsThreadSleep(0.1);

    testdbGetFieldEqual("src:li1.VAL", DBF_LONG, 42);

    //TODO: how to distinguish "record.FLD" from pva "channel.subfield"?
    testdbPutFieldOk("src:li1.INP", DBF_STRING, "{\"pva\":\"target:ai\"}");

    testdbGetFieldEqual("src:li1.VAL", DBF_LONG, 42);

    //TODO: wait for pvalink worker update
    epicsThreadSleep(0.1);
    testdbPutFieldOk("src:li1.PROC", DBF_LONG, 1);
    //TODO: wait for dbEvent queue update
    epicsThreadSleep(0.1);

    testdbGetFieldEqual("src:li1.VAL", DBF_LONG, 4);
}

} // namespace

extern "C"
void p2pTestIoc_registerRecordDeviceDriver(struct dbBase *);

MAIN(testpvalink)
{
    testPlan(0);

    // Disable PVA client provider, use local/QSRV provider
    pvaLinkIsolate = 1;
    pvaLinkDebug = 5;

    try {
        TestIOC IOC;

        testdbReadDatabase("p2pTestIoc.dbd", NULL, NULL);
        p2pTestIoc_registerRecordDeviceDriver(pdbbase);
        testdbReadDatabase("testpvalink.db", NULL, NULL);

        IOC.init();
        testGet();
        IOC.shutdown();

    }catch(std::exception& e){
        testFail("Unexpected exception: %s", e.what());
    }
    // call epics atexits explicitly as workaround for c++ static dtor issues...
    epicsExit(testDone());
}
