
#include <testMain.h>

#include <dbAccess.h>
#include <longinRecord.h>
#include <aiRecord.h>
#include <mbbiRecord.h>

#include "pvif.h"
#include "utilities.h"

namespace pvd = epics::pvData;

extern "C"
void p2pTestIoc_registerRecordDeviceDriver(struct dbBase *);

namespace {

void testScalar()
{
    testDiag("======= testScalar() ======");

    TestIOC IOC;

    testdbReadDatabase("p2pTestIoc.dbd", NULL, NULL);
    p2pTestIoc_registerRecordDeviceDriver(pdbbase);
    testdbReadDatabase("testpvif.db", NULL, NULL);

    longinRecord *prec_li = (longinRecord*)testdbRecordPtr("test:li");
    aiRecord *prec_ai = (aiRecord*)testdbRecordPtr("test:ai");
    mbbiRecord *prec_mbbi = (mbbiRecord*)testdbRecordPtr("test:mbbi");

    IOC.init();

    testdbGetFieldEqual("test:mbbi", DBR_STRING, "one");
    testdbGetFieldEqual("test:mbbi", DBR_SHORT, 1);

    DBCH chan_li("test:li");
    DBCH chan_ai("test:ai");
    DBCH chan_ai_rval("test:ai.RVAL");
    DBCH chan_mbbi("test:mbbi");
    testEqual(dbChannelFieldType(chan_li), DBR_LONG);
    testEqual(dbChannelFieldType(chan_ai), DBR_DOUBLE);
    testEqual(dbChannelFieldType(chan_ai_rval), DBR_LONG);
    testEqual(dbChannelFieldType(chan_mbbi), DBR_ENUM);
    testEqual(dbChannelFinalFieldType(chan_li), DBR_LONG);
    testEqual(dbChannelFinalFieldType(chan_ai), DBR_DOUBLE);
    testEqual(dbChannelFinalFieldType(chan_ai_rval), DBR_LONG);
    testEqual(dbChannelFinalFieldType(chan_mbbi), DBR_ENUM);

    pvd::StructureConstPtr dtype_li(PVIF::dtype(chan_li));
    pvd::StructureConstPtr dtype_ai(PVIF::dtype(chan_ai));
    pvd::StructureConstPtr dtype_ai_rval(PVIF::dtype(chan_ai_rval));
    pvd::StructureConstPtr dtype_mbbi(PVIF::dtype(chan_mbbi));

    pvd::StructureConstPtr dtype_root(pvd::getFieldCreate()->createFieldBuilder()
                                      ->add("li", dtype_li)
                                      ->add("ai", dtype_ai)
                                      ->add("ai_rval", dtype_ai_rval)
                                      ->add("mbbi", dtype_mbbi)
                                      ->createStructure());

    pvd::PVStructurePtr root(pvd::getPVDataCreate()->createPVStructure(dtype_root));

    std::auto_ptr<PVIF> pvif_li(PVIF::attach(chan_li, root->getSubField<pvd::PVStructure>("li")));
    std::auto_ptr<PVIF> pvif_ai(PVIF::attach(chan_ai, root->getSubField<pvd::PVStructure>("ai")));
    std::auto_ptr<PVIF> pvif_ai_rval(PVIF::attach(chan_ai_rval, root->getSubField<pvd::PVStructure>("ai_rval")));
    std::auto_ptr<PVIF> pvif_mbbi(PVIF::attach(chan_mbbi, root->getSubField<pvd::PVStructure>("mbbi")));

    pvd::BitSet mask;

    dbScanLock((dbCommon*)prec_li);
    prec_li->time.secPastEpoch = 0x12345678;
    prec_li->time.nsec = 12345678;
    pvif_li->put(mask, DBE_VALUE|DBE_ALARM|DBE_PROPERTY, NULL);
    dbScanUnlock((dbCommon*)prec_li);

    dbScanLock((dbCommon*)prec_ai);
    prec_ai->time.secPastEpoch = 0x12345678;
    prec_ai->time.nsec = 12345678;
    pvif_ai->put(mask, DBE_VALUE|DBE_ALARM|DBE_PROPERTY, NULL);
    pvif_ai_rval->put(mask, DBE_VALUE|DBE_ALARM|DBE_PROPERTY, NULL);
    dbScanUnlock((dbCommon*)prec_ai);

    dbScanLock((dbCommon*)prec_mbbi);
    prec_mbbi->time.secPastEpoch = 0x12345678;
    prec_mbbi->time.nsec = 12345678;
    pvif_mbbi->put(mask, DBE_VALUE|DBE_ALARM|DBE_PROPERTY, NULL);
    dbScanUnlock((dbCommon*)prec_mbbi);

    testFieldEqual<pvd::PVInt>(root, "li.value", 102042);
    testFieldEqual<pvd::PVInt>(root, "li.alarm.severity", 1);
    testFieldEqual<pvd::PVLong>(root, "li.timeStamp.secondsPastEpoch", 0x12345678);
    testFieldEqual<pvd::PVInt>(root, "li.timeStamp.nanoseconds", 12345678);
    testFieldEqual<pvd::PVDouble>(root, "li.display.limitHigh", 100.0);
    testFieldEqual<pvd::PVDouble>(root, "li.display.limitLow", 10.0);
    testFieldEqual<pvd::PVString>(root, "li.display.units", "arb");

    testFieldEqual<pvd::PVDouble>(root, "ai.value", 42.2);
    testFieldEqual<pvd::PVInt>(root, "ai.alarm.severity", 2);
    testFieldEqual<pvd::PVLong>(root, "ai.timeStamp.secondsPastEpoch", 0x12345678);
    testFieldEqual<pvd::PVInt>(root, "ai.timeStamp.nanoseconds", 12345678);
    testFieldEqual<pvd::PVDouble>(root, "ai.display.limitHigh", 200.0);
    testFieldEqual<pvd::PVDouble>(root, "ai.display.limitLow", 20.0);
    testFieldEqual<pvd::PVString>(root, "ai.display.format", "2");
    testFieldEqual<pvd::PVString>(root, "ai.display.units", "foo");

    testFieldEqual<pvd::PVInt>(root, "ai_rval.value", 1234);
    testFieldEqual<pvd::PVInt>(root, "ai_rval.alarm.severity", 2);
    testFieldEqual<pvd::PVLong>(root, "ai_rval.timeStamp.secondsPastEpoch", 0x12345678);
    testFieldEqual<pvd::PVInt>(root, "ai_rval.timeStamp.nanoseconds", 12345678);
    testFieldEqual<pvd::PVDouble>(root, "ai_rval.display.limitHigh", 2147483647.0);
    testFieldEqual<pvd::PVDouble>(root, "ai_rval.display.limitLow", -2147483648.0);
    testFieldEqual<pvd::PVString>(root, "ai_rval.display.format", "");
    testFieldEqual<pvd::PVString>(root, "ai_rval.display.units", "");

    testFieldEqual<pvd::PVInt>(root, "mbbi.value.index", 1);
    testFieldEqual<pvd::PVInt>(root, "mbbi.alarm.severity", 0);
    testFieldEqual<pvd::PVLong>(root, "mbbi.timeStamp.secondsPastEpoch", 0x12345678);
    testFieldEqual<pvd::PVInt>(root, "mbbi.timeStamp.nanoseconds", 12345678);
    {
        pvd::PVStringArray::const_svector choices(root->getSubFieldT<pvd::PVStringArray>("mbbi.value.choices")->view());
        testOk1(choices.size()==3);
        testOk1(choices.size()>0 && choices[0]=="zero");
        testOk1(choices.size()>1 && choices[1]=="one");
        testOk1(choices.size()>2 && choices[2]=="two");
    }

    root->getSubFieldT<pvd::PVInt>("li.value")->put(102043);
    root->getSubFieldT<pvd::PVDouble>("ai.value")->put(44.4);
    root->getSubFieldT<pvd::PVInt>("mbbi.value.index")->put(2);

    dbScanLock((dbCommon*)prec_li);
    pvif_li->get(mask);
    testOk1(prec_li->val==102043);
    dbScanUnlock((dbCommon*)prec_li);

    dbScanLock((dbCommon*)prec_ai);
    pvif_ai->get(mask);
    testOk1(prec_ai->val==44.4);
    dbScanUnlock((dbCommon*)prec_ai);

    dbScanLock((dbCommon*)prec_mbbi);
    pvif_mbbi->get(mask);
    testOk1(prec_mbbi->val==2);
    dbScanUnlock((dbCommon*)prec_mbbi);
}

} // namespace

MAIN(testpvif)
{
    testPlan(0);
    PVIF::Init();
    testScalar();
    return testDone();
}
