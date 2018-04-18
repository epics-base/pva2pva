
#include <testMain.h>

#include <iocsh.h>
#include <dbAccess.h>
#include <longinRecord.h>
#include <aiRecord.h>
#include <mbbiRecord.h>
#include <stringinRecord.h>

#include "helper.h"
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
    stringinRecord *prec_si = (stringinRecord*)testdbRecordPtr("test:si");
    aiRecord *prec_ai = (aiRecord*)testdbRecordPtr("test:ai");
    mbbiRecord *prec_mbbi = (mbbiRecord*)testdbRecordPtr("test:mbbi");

    IOC.init();

    testdbGetFieldEqual("test:mbbi", DBR_STRING, "one");
    testdbGetFieldEqual("test:mbbi", DBR_SHORT, 1);

    DBCH chan_li("test:li");
    DBCH chan_si("test:si");
    DBCH chan_ai("test:ai");
    DBCH chan_ai_rval("test:ai.RVAL");
    DBCH chan_mbbi("test:mbbi");
    testEqual(dbChannelFieldType(chan_li), DBR_LONG);
    testEqual(dbChannelFieldType(chan_si), DBR_STRING);
    testEqual(dbChannelFieldType(chan_ai), DBR_DOUBLE);
    testEqual(dbChannelFieldType(chan_ai_rval), DBR_LONG);
    testEqual(dbChannelFieldType(chan_mbbi), DBR_ENUM);
    testEqual(dbChannelFinalFieldType(chan_li), DBR_LONG);
    testEqual(dbChannelFinalFieldType(chan_ai), DBR_DOUBLE);
    testEqual(dbChannelFinalFieldType(chan_ai_rval), DBR_LONG);
    testEqual(dbChannelFinalFieldType(chan_mbbi), DBR_ENUM);

    ScalarBuilder builder;

    pvd::FieldConstPtr dtype_li(builder.dtype(chan_li));
    pvd::FieldConstPtr dtype_si(builder.dtype(chan_si));
    pvd::FieldConstPtr dtype_ai(builder.dtype(chan_ai));
    pvd::FieldConstPtr dtype_ai_rval(builder.dtype(chan_ai_rval));
    pvd::FieldConstPtr dtype_mbbi(builder.dtype(chan_mbbi));

    pvd::StructureConstPtr dtype_root(pvd::getFieldCreate()->createFieldBuilder()
                                      ->add("li", dtype_li)
                                      ->add("si", dtype_si)
                                      ->add("ai", dtype_ai)
                                      ->add("ai_rval", dtype_ai_rval)
                                      ->add("mbbi", dtype_mbbi)
                                      ->createStructure());

    pvd::PVStructurePtr root(pvd::getPVDataCreate()->createPVStructure(dtype_root));

    p2p::auto_ptr<PVIF> pvif_li(builder.attach(chan_li, root, FieldName("li")));
    p2p::auto_ptr<PVIF> pvif_si(builder.attach(chan_si, root, FieldName("si")));
    p2p::auto_ptr<PVIF> pvif_ai(builder.attach(chan_ai, root, FieldName("ai")));
    p2p::auto_ptr<PVIF> pvif_ai_rval(builder.attach(chan_ai_rval, root, FieldName("ai_rval")));
    p2p::auto_ptr<PVIF> pvif_mbbi(builder.attach(chan_mbbi, root, FieldName("mbbi")));

    pvd::BitSet mask;

    dbScanLock((dbCommon*)prec_li);
    prec_li->time.secPastEpoch = 0x12345678;
    prec_li->time.nsec = 12345678;
    pvif_li->put(mask, DBE_VALUE|DBE_ALARM|DBE_PROPERTY, NULL);
    dbScanUnlock((dbCommon*)prec_li);

#define OFF(NAME) (epicsUInt32)root->getSubFieldT(NAME)->getFieldOffset()
    testEqual(mask, pvd::BitSet()
              .set(OFF("li.value"))
              .set(OFF("li.alarm.severity"))
              .set(OFF("li.alarm.status"))
              .set(OFF("li.alarm.message"))
              .set(OFF("li.timeStamp.secondsPastEpoch"))
              .set(OFF("li.timeStamp.nanoseconds"))
              //.set(OFF("li.timeStamp.userTag"))
              .set(OFF("li.display.limitHigh"))
              .set(OFF("li.display.limitLow"))
              .set(OFF("li.display.description"))
              .set(OFF("li.display.format"))
              .set(OFF("li.display.units"))
              .set(OFF("li.control.limitHigh"))
              .set(OFF("li.control.limitLow"))
              .set(OFF("li.valueAlarm.highWarningLimit"))
              .set(OFF("li.valueAlarm.lowWarningLimit"))
              .set(OFF("li.valueAlarm.highAlarmLimit"))
              .set(OFF("li.valueAlarm.lowAlarmLimit")));
#undef OFF
    mask.clear();

    dbScanLock((dbCommon*)prec_si);
    prec_si->time.secPastEpoch = 0x12345678;
    prec_si->time.nsec = 12345678;
    pvif_si->put(mask, DBE_VALUE|DBE_ALARM|DBE_PROPERTY, NULL);
    dbScanUnlock((dbCommon*)prec_si);

#define OFF(NAME) (epicsUInt32)root->getSubFieldT(NAME)->getFieldOffset()
    testEqual(mask, pvd::BitSet()
              .set(OFF("si.value"))
              .set(OFF("si.alarm.severity"))
              .set(OFF("si.alarm.status"))
              .set(OFF("si.alarm.message"))
              .set(OFF("si.timeStamp.secondsPastEpoch"))
              .set(OFF("si.timeStamp.nanoseconds"))
              //.set(OFF("si.timeStamp.userTag"))
              .set(OFF("si.display.limitHigh"))
              .set(OFF("si.display.limitLow"))
              .set(OFF("si.display.description"))
              .set(OFF("si.display.format"))
              .set(OFF("si.display.units"))
              .set(OFF("si.control.limitHigh"))
              .set(OFF("si.control.limitLow")));
#undef OFF
    mask.clear();

    dbScanLock((dbCommon*)prec_ai);
    prec_ai->time.secPastEpoch = 0x12345678;
    prec_ai->time.nsec = 12345678;
    pvif_ai->put(mask, DBE_VALUE|DBE_ALARM|DBE_PROPERTY, NULL);
    pvif_ai_rval->put(mask, DBE_VALUE|DBE_ALARM|DBE_PROPERTY, NULL);
    dbScanUnlock((dbCommon*)prec_ai);

#define OFF(NAME) (epicsUInt32)root->getSubFieldT(NAME)->getFieldOffset()
    testEqual(mask, pvd::BitSet()
              .set(OFF("ai.value"))
              .set(OFF("ai.alarm.severity"))
              .set(OFF("ai.alarm.status"))
              .set(OFF("ai.alarm.message"))
              .set(OFF("ai.timeStamp.secondsPastEpoch"))
              .set(OFF("ai.timeStamp.nanoseconds"))
              //.set(OFF("ai.timeStamp.userTag"))
              .set(OFF("ai.display.limitHigh"))
              .set(OFF("ai.display.limitLow"))
              .set(OFF("ai.display.description"))
              .set(OFF("ai.display.format"))
              .set(OFF("ai.display.units"))
              .set(OFF("ai.control.limitHigh"))
              .set(OFF("ai.control.limitLow"))
              .set(OFF("ai.valueAlarm.highWarningLimit"))
              .set(OFF("ai.valueAlarm.lowWarningLimit"))
              .set(OFF("ai.valueAlarm.highAlarmLimit"))
              .set(OFF("ai.valueAlarm.lowAlarmLimit"))
              .set(OFF("ai_rval.value"))
              .set(OFF("ai_rval.alarm.severity"))
              .set(OFF("ai_rval.alarm.status"))
              .set(OFF("ai_rval.alarm.message"))
              .set(OFF("ai_rval.timeStamp.secondsPastEpoch"))
              .set(OFF("ai_rval.timeStamp.nanoseconds"))
              //.set(OFF("ai_rval.timeStamp.userTag"))
              .set(OFF("ai_rval.display.limitHigh"))
              .set(OFF("ai_rval.display.limitLow"))
              .set(OFF("ai_rval.display.description"))
              .set(OFF("ai_rval.display.format"))
              .set(OFF("ai_rval.display.units"))
              .set(OFF("ai_rval.control.limitHigh"))
              .set(OFF("ai_rval.control.limitLow"))
              .set(OFF("ai_rval.valueAlarm.highWarningLimit"))
              .set(OFF("ai_rval.valueAlarm.lowWarningLimit"))
              .set(OFF("ai_rval.valueAlarm.highAlarmLimit"))
              .set(OFF("ai_rval.valueAlarm.lowAlarmLimit"))
              );
#undef OFF
    mask.clear();

    dbScanLock((dbCommon*)prec_mbbi);
    prec_mbbi->time.secPastEpoch = 0x12345678;
    prec_mbbi->time.nsec = 0x12345678;
    pvif_mbbi->put(mask, DBE_VALUE|DBE_ALARM|DBE_PROPERTY, NULL);
    dbScanUnlock((dbCommon*)prec_mbbi);

#define OFF(NAME) root->getSubFieldT(NAME)->getFieldOffset()
    testEqual(mask, pvd::BitSet()
              .set(OFF("mbbi.value.index"))
              .set(OFF("mbbi.value.choices"))
              .set(OFF("mbbi.alarm.severity"))
              .set(OFF("mbbi.alarm.status"))
              .set(OFF("mbbi.alarm.message"))
              .set(OFF("mbbi.timeStamp.secondsPastEpoch"))
              .set(OFF("mbbi.timeStamp.nanoseconds"))
              .set(OFF("mbbi.timeStamp.userTag")));
#undef OFF
    mask.clear();

    testFieldEqual<pvd::PVInt>(root, "li.value", 102042);
    testFieldEqual<pvd::PVInt>(root, "li.alarm.severity", 1);
    testFieldEqual<pvd::PVInt>(root, "li.alarm.status", 1);
    testFieldEqual<pvd::PVLong>(root, "li.timeStamp.secondsPastEpoch", 0x12345678+POSIX_TIME_AT_EPICS_EPOCH);
    testFieldEqual<pvd::PVInt>(root, "li.timeStamp.nanoseconds", 12345678);
    testFieldEqual<pvd::PVDouble>(root, "li.display.limitHigh", 100.0);
    testFieldEqual<pvd::PVDouble>(root, "li.display.limitLow", 10.0);
    testFieldEqual<pvd::PVString>(root, "li.display.units", "arb");

    testFieldEqual<pvd::PVString>(root, "si.value", "hello");
    testFieldEqual<pvd::PVInt>(root, "si.alarm.severity", 0);
    testFieldEqual<pvd::PVLong>(root, "si.timeStamp.secondsPastEpoch", 0x12345678+POSIX_TIME_AT_EPICS_EPOCH);
    testFieldEqual<pvd::PVInt>(root, "si.timeStamp.nanoseconds", 12345678);

    testFieldEqual<pvd::PVDouble>(root, "ai.value", 42.2);
    testFieldEqual<pvd::PVInt>(root, "ai.alarm.severity", 2);
    testFieldEqual<pvd::PVInt>(root, "ai.alarm.status", 1);
    testFieldEqual<pvd::PVLong>(root, "ai.timeStamp.secondsPastEpoch", 0x12345678+POSIX_TIME_AT_EPICS_EPOCH);
    testFieldEqual<pvd::PVInt>(root, "ai.timeStamp.nanoseconds", 12345678);
    testFieldEqual<pvd::PVDouble>(root, "ai.display.limitHigh", 200.0);
    testFieldEqual<pvd::PVDouble>(root, "ai.display.limitLow", 20.0);
    testFieldEqual<pvd::PVString>(root, "ai.display.format", "");
    testFieldEqual<pvd::PVString>(root, "ai.display.units", "foo");

    testFieldEqual<pvd::PVInt>(root, "ai_rval.value", 1234);
    testFieldEqual<pvd::PVInt>(root, "ai_rval.alarm.severity", 2);
    testFieldEqual<pvd::PVLong>(root, "ai_rval.timeStamp.secondsPastEpoch", 0x12345678+POSIX_TIME_AT_EPICS_EPOCH);
    testFieldEqual<pvd::PVInt>(root, "ai_rval.timeStamp.nanoseconds", 12345678);
    testFieldEqual<pvd::PVInt>(root, "ai_rval.timeStamp.userTag", 0);
    testFieldEqual<pvd::PVDouble>(root, "ai_rval.display.limitHigh", 2147483647.0);
    testFieldEqual<pvd::PVDouble>(root, "ai_rval.display.limitLow", -2147483648.0);
    testFieldEqual<pvd::PVString>(root, "ai_rval.display.format", "");
    testFieldEqual<pvd::PVString>(root, "ai_rval.display.units", "");

    testFieldEqual<pvd::PVInt>(root, "mbbi.value.index", 1);
    testFieldEqual<pvd::PVInt>(root, "mbbi.alarm.severity", 0);
    testFieldEqual<pvd::PVLong>(root, "mbbi.timeStamp.secondsPastEpoch", 0x12345678+POSIX_TIME_AT_EPICS_EPOCH);
    testFieldEqual<pvd::PVInt>(root, "mbbi.timeStamp.nanoseconds", 0x12345670);
    testFieldEqual<pvd::PVInt>(root, "mbbi.timeStamp.userTag", 0x8);
    {
        pvd::PVStringArray::const_svector choices(root->getSubFieldT<pvd::PVStringArray>("mbbi.value.choices")->view());
        testOk1(choices.size()==3);
        testOk1(choices.size()>0 && choices[0]=="zero");
        testOk1(choices.size()>1 && choices[1]=="one");
        testOk1(choices.size()>2 && choices[2]=="two");
    }

    root->getSubFieldT<pvd::PVInt>("li.value")->put(102043);
    root->getSubFieldT<pvd::PVString>("si.value")->put("world");
    root->getSubFieldT<pvd::PVDouble>("ai.value")->put(44.4);
    root->getSubFieldT<pvd::PVInt>("ai_rval.value")->put(2143);
    root->getSubFieldT<pvd::PVInt>("mbbi.value.index")->put(2);

    dbScanLock((dbCommon*)prec_li);
    mask.clear();
    mask.set(root->getSubFieldT("li.value")->getFieldOffset());
    pvif_li->get(mask);
    testEqual(prec_li->val, 102043);
    dbScanUnlock((dbCommon*)prec_li);

    dbScanLock((dbCommon*)prec_si);
    mask.clear();
    mask.set(root->getSubFieldT("si.value")->getFieldOffset());
    pvif_si->get(mask);
    testOk(strcmp(prec_si->val, "world")==0, "\"%s\" == \"%s\"", prec_si->val, "world");
    dbScanUnlock((dbCommon*)prec_si);

    dbScanLock((dbCommon*)prec_ai);
    mask.clear();
    mask.set(root->getSubFieldT("ai.value")->getFieldOffset());
    pvif_ai->get(mask);
    testEqual(prec_ai->val, 44.4);
    dbScanUnlock((dbCommon*)prec_ai);

    dbScanLock((dbCommon*)prec_ai);
    mask.clear();
    mask.set(root->getSubFieldT("ai_rval.value")->getFieldOffset());
    pvif_ai_rval->get(mask);
    testEqual(prec_ai->rval, 2143);
    dbScanUnlock((dbCommon*)prec_ai);

    dbScanLock((dbCommon*)prec_mbbi);
    mask.clear();
    mask.set(root->getSubFieldT("mbbi.value.index")->getFieldOffset());
    pvif_mbbi->get(mask);
    testEqual(prec_mbbi->val, 2);
    dbScanUnlock((dbCommon*)prec_mbbi);
}

void testPlain()
{
    testDiag("testPlain()");

    TestIOC IOC;

    testdbReadDatabase("p2pTestIoc.dbd", NULL, NULL);
    p2pTestIoc_registerRecordDeviceDriver(pdbbase);
    testdbReadDatabase("testpvif.db", NULL, NULL);

    longinRecord *prec_li = (longinRecord*)testdbRecordPtr("test:li");
    stringinRecord *prec_si = (stringinRecord*)testdbRecordPtr("test:si");
    aiRecord *prec_ai = (aiRecord*)testdbRecordPtr("test:ai");
    mbbiRecord *prec_mbbi = (mbbiRecord*)testdbRecordPtr("test:mbbi");

    IOC.init();

    DBCH chan_li("test:li");
    DBCH chan_si("test:si");
    DBCH chan_ai("test:ai");
    DBCH chan_mbbi("test:mbbi");

    p2p::auto_ptr<PVIFBuilder> builder;
    {
        builder.reset(PVIFBuilder::create("plain"));
    }

    pvd::FieldConstPtr dtype_li(builder->dtype(chan_li));
    pvd::FieldConstPtr dtype_si(builder->dtype(chan_si));
    pvd::FieldConstPtr dtype_ai(builder->dtype(chan_ai));
    pvd::FieldConstPtr dtype_mbbi(builder->dtype(chan_mbbi));

    pvd::StructureConstPtr dtype_root(pvd::getFieldCreate()->createFieldBuilder()
                                      ->add("li", dtype_li)
                                      ->add("si", dtype_si)
                                      ->add("ai", dtype_ai)
                                      ->add("mbbi", dtype_mbbi)
                                      ->createStructure());

    pvd::PVStructurePtr root(pvd::getPVDataCreate()->createPVStructure(dtype_root));

    p2p::auto_ptr<PVIF> pvif_li(builder->attach(chan_li, root, FieldName("li")));
    p2p::auto_ptr<PVIF> pvif_si(builder->attach(chan_si, root, FieldName("si")));
    p2p::auto_ptr<PVIF> pvif_ai(builder->attach(chan_ai, root, FieldName("ai")));
    p2p::auto_ptr<PVIF> pvif_mbbi(builder->attach(chan_mbbi, root, FieldName("mbbi")));

    pvd::BitSet mask;

    mask.clear();
    dbScanLock((dbCommon*)prec_li);
    pvif_li->put(mask, DBE_VALUE, NULL);
    dbScanUnlock((dbCommon*)prec_li);

    testEqual(mask, pvd::BitSet().set(root->getSubFieldT("li")->getFieldOffset()));

    testFieldEqual<pvd::PVInt>(root, "li", 102042);

    mask.clear();
    dbScanLock((dbCommon*)prec_si);
    pvif_si->put(mask, DBE_VALUE, NULL);
    dbScanUnlock((dbCommon*)prec_si);

    testEqual(mask, pvd::BitSet().set(root->getSubFieldT("si")->getFieldOffset()));

    testFieldEqual<pvd::PVString>(root, "si", "hello");

    mask.clear();
    dbScanLock((dbCommon*)prec_ai);
    pvif_ai->put(mask, DBE_VALUE, NULL);
    dbScanUnlock((dbCommon*)prec_ai);

    testEqual(mask, pvd::BitSet().set(root->getSubFieldT("ai")->getFieldOffset()));

    testFieldEqual<pvd::PVDouble>(root, "ai", 42.2);

    mask.clear();
    dbScanLock((dbCommon*)prec_mbbi);
    pvif_mbbi->put(mask, DBE_VALUE, NULL);
    dbScanUnlock((dbCommon*)prec_mbbi);

    testEqual(mask, pvd::BitSet().set(root->getSubFieldT("mbbi")->getFieldOffset()));

    testFieldEqual<pvd::PVInt>(root, "mbbi", 1);


    root->getSubFieldT<pvd::PVInt>("li")->put(102043);
    root->getSubFieldT<pvd::PVString>("si")->put("world");
    root->getSubFieldT<pvd::PVDouble>("ai")->put(44.4);
    root->getSubFieldT<pvd::PVInt>("mbbi")->put(2);

    dbScanLock((dbCommon*)prec_li);
    mask.clear();
    mask.set(root->getSubFieldT("li")->getFieldOffset());
    pvif_li->get(mask);
    testEqual(prec_li->val, 102043);
    dbScanUnlock((dbCommon*)prec_li);

    dbScanLock((dbCommon*)prec_si);
    mask.clear();
    mask.set(root->getSubFieldT("si")->getFieldOffset());
    pvif_si->get(mask);
    testOk(strcmp(prec_si->val, "world")==0, "\"%s\" == \"%s\"", prec_si->val, "world");
    dbScanUnlock((dbCommon*)prec_si);

    dbScanLock((dbCommon*)prec_ai);
    mask.clear();
    mask.set(root->getSubFieldT("ai")->getFieldOffset());
    pvif_ai->get(mask);
    testEqual(prec_ai->val, 44.4);
    dbScanUnlock((dbCommon*)prec_ai);

    dbScanLock((dbCommon*)prec_mbbi);
    mask.clear();
    mask.set(root->getSubFieldT("mbbi")->getFieldOffset());
    pvif_mbbi->get(mask);
    testEqual(prec_mbbi->val, 2);
    dbScanUnlock((dbCommon*)prec_mbbi);
}

} // namespace

MAIN(testpvif)
{
    testPlan(71);
    testScalar();
    testPlain();
    return testDone();
}
