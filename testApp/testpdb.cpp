
#include <testMain.h>

#include <iocsh.h>
#include <epicsAtomic.h>
#include <dbAccess.h>

#include <pv/epicsException.h>

#include "utilities.h"
#include "pvif.h"
#include "pdb.h"
#include "pdbsingle.h"
#ifdef USE_MULTILOCK
#  include "pdbgroup.h"
#endif

namespace pvd = epics::pvData;
namespace pva = epics::pvAccess;

namespace {

pvd::PVStructurePtr makeRequest(bool atomic)
{    pvd::StructureConstPtr def(pvd::getFieldCreate()->createFieldBuilder()
                                ->addNestedStructure("record")
                                    ->addNestedStructure("_options")
                                        ->add("atomic", pvd::pvBoolean)
                                        ->endNested()
                                    ->endNested()
                                ->createStructure());
     pvd::PVStructurePtr pvr(pvd::getPVDataCreate()->createPVStructure(def));
     pvr->getSubFieldT<pvd::PVBoolean>("record._options.atomic")->put(atomic);
    return pvr;
}

struct PVConnect
{
    TestChannelRequester::shared_pointer chreq;
    pva::Channel::shared_pointer chan;
    TestChannelFieldRequester::shared_pointer fldreq;

    PVConnect(const pva::ChannelProvider::shared_pointer& prov, const char *name)
        :chreq(new TestChannelRequester())
        ,chan(prov->createChannel(name, chreq))
        ,fldreq(new TestChannelFieldRequester())
    {
        if(!chan || !chan->isConnected())
            throw std::runtime_error("channel not connected");
        chan->getField(fldreq, "");
        if(!fldreq->done || !fldreq->fielddesc || fldreq->fielddesc->getType()!=pvd::structure)
            throw std::runtime_error("Failed to get fielddesc");
    }
    virtual ~PVConnect() {
        chan->destroy();
    }
    pvd::StructureConstPtr dtype() {
        return std::tr1::static_pointer_cast<const pvd::Structure>(fldreq->fielddesc);
    }
};

struct PVPut : public PVConnect
{
    pvd::PVStructurePtr putval;
    TestChannelPutRequester::shared_pointer putreq;
    pva::ChannelPut::shared_pointer chput;
    pvd::BitSetPtr putchanged;

    PVPut(const pva::ChannelProvider::shared_pointer& prov, const char *name, bool atomic=true)
        :PVConnect(prov, name)
        ,putval(pvd::getPVDataCreate()->createPVStructure(dtype()))
        ,putreq(new TestChannelPutRequester())
        ,chput(chan->createChannelPut(putreq, makeRequest(atomic)))
        ,putchanged(new pvd::BitSet())
    {
        if(!chput || !putreq->connected)
            throw std::runtime_error("Failed to create/connect put op");
    }
    virtual ~PVPut() {
        chput->destroy();
    }
    void put() {
        putreq->donePut = false;
        chput->put(putval, putchanged);
        if(!putreq->donePut)
            throw std::runtime_error("Put operation fails");
    }
    pvd::PVStructurePtr get() {
        putreq->doneGet = false;
        chput->get();
        if(!putreq->doneGet || !putreq->value)
            throw std::runtime_error("Get operation fails");
        return putreq->value;
    }
};

struct PVMonitor : public PVConnect
{
    POINTER_DEFINITIONS(PVMonitor);

    TestChannelMonitorRequester::shared_pointer monreq;
    pva::Monitor::shared_pointer mon;

    PVMonitor(const pva::ChannelProvider::shared_pointer& prov, const char *name)
        :PVConnect(prov, name)
        ,monreq(new TestChannelMonitorRequester())
        ,mon(chan->createMonitor(monreq, makeRequest(false)))
    {
        if(!mon || !monreq->connected)
            throw std::runtime_error("Failed to create/connect monitor");
    }
    virtual ~PVMonitor() {
        mon->destroy();
    }

    pva::MonitorElementPtr poll() { return mon->poll(); }
};

pvd::PVStructurePtr pvget(const pva::ChannelProvider::shared_pointer& prov, const char *name,
                          bool atomic)
{
    pvd::PVStructurePtr pvr(makeRequest(atomic));


    TestChannelRequester::shared_pointer req(new TestChannelRequester());
    pva::Channel::shared_pointer chan(prov->createChannel(name, req));

    testOk1(!!chan);
    testOk1(chan && chan->isConnected());
    testOk1(req->laststate == pva::Channel::CONNECTED);
    testOk1(req->status.isOK());
    if(!chan || !chan->isConnected())
        testAbort("'%s' channel not connected", name);

    TestChannelGetRequester::shared_pointer greq(new TestChannelGetRequester());
    pva::ChannelGet::shared_pointer get(chan->createChannelGet(greq, pvr));

    testOk1(!!get);
    testOk1(greq->connected);
    testOk1(!greq->done);
    testOk1(greq->statusDone.isOK());
    if(!greq || !greq->connected)
        testAbort("'%s channelGet not connected", name);

    get->get();

    testOk1(greq->done);
    testOk1(greq->statusDone.isOK());
    testOk1(!!greq->value);
    if(!greq->value)
        testAbort("'%s' get w/o data", name);

    get->destroy();
    chan->destroy();

    return greq->value;
}

void testSingleGet(const PDBProvider::shared_pointer& prov)
{
    testDiag("test single get");
    pvd::PVStructurePtr value;

    value = pvget(prov, "rec1", false);
    testFieldEqual<pvd::PVDouble>(value, "value", 1.0);
    testFieldEqual<pvd::PVDouble>(value, "display.limitHigh", 100.0);
    testFieldEqual<pvd::PVDouble>(value, "display.limitLow", -100.0);

    value = pvget(prov, "rec1.RVAL", false);
    testFieldEqual<pvd::PVInt>(value, "value", 10);
}

void testGroupGet(const PDBProvider::shared_pointer& prov)
{
    testDiag("test group get");
#ifdef USE_MULTILOCK
    pvd::PVStructurePtr value;

    testDiag("get non-atomic");
    value = pvget(prov, "grp1", false);
    testFieldEqual<pvd::PVDouble>(value, "fld1.value", 3.0);
    testFieldEqual<pvd::PVInt>(value,    "fld2.value", 30);
    testFieldEqual<pvd::PVDouble>(value, "fld3.value", 4.0);
    testFieldEqual<pvd::PVInt>(value,    "fld4.value", 40);

    testDiag("get atomic");
    value = pvget(prov, "grp1", true);
    testFieldEqual<pvd::PVDouble>(value, "fld1.value", 3.0);
    testFieldEqual<pvd::PVInt>(value,    "fld2.value", 30);
    testFieldEqual<pvd::PVDouble>(value, "fld3.value", 4.0);
    testFieldEqual<pvd::PVInt>(value,    "fld4.value", 40);
#else
    testSkip(30, "No multilock");
#endif
}

void testSinglePut(const PDBProvider::shared_pointer& prov)
{
    testDiag("test single put");

    testdbPutFieldOk("rec1", DBR_DOUBLE, 1.0);

    PVPut put(prov, "rec1.VAL");

    pvd::PVDoublePtr val(put.putval->getSubFieldT<pvd::PVDouble>("value"));
    val->put(2.0);
    put.putchanged->clear();
    put.put();

    testdbGetFieldEqual("rec1", DBR_DOUBLE, 1.0);

    put.putchanged->set(val->getFieldOffset());
    put.put();

    testdbGetFieldEqual("rec1", DBR_DOUBLE, 2.0);
}

void testGroupPut(const PDBProvider::shared_pointer& prov)
{
    testDiag("test group put");
#ifdef USE_MULTILOCK

    testdbPutFieldOk("rec3", DBR_DOUBLE, 3.0);
    testdbPutFieldOk("rec4", DBR_DOUBLE, 4.0);
    testdbPutFieldOk("rec3.RVAL", DBR_LONG, 30);
    testdbPutFieldOk("rec4.RVAL", DBR_LONG, 40);

    PVPut put(prov, "grp1");

    pvd::PVDoublePtr val(put.putval->getSubFieldT<pvd::PVDouble>("fld1.value"));
    val->put(2.0);
    put.putchanged->clear();
    // putchanged is clear, so no change
    put.put();

    testdbGetFieldEqual("rec3", DBR_DOUBLE, 3.0);
    testdbGetFieldEqual("rec4", DBR_DOUBLE, 4.0);
    testdbGetFieldEqual("rec3.RVAL", DBR_LONG, 30);
    testdbGetFieldEqual("rec4.RVAL", DBR_LONG, 40);

    val = put.putval->getSubFieldT<pvd::PVDouble>("fld3.value");
    val->put(5.0);
    put.putchanged->clear();
    // mark fld3, but still not fld1
    put.putchanged->set(val->getFieldOffset());
    put.put();

    testdbGetFieldEqual("rec3", DBR_DOUBLE, 3.0);
    testdbGetFieldEqual("rec4", DBR_DOUBLE, 5.0);
    testdbGetFieldEqual("rec3.RVAL", DBR_LONG, 30);
    testdbGetFieldEqual("rec4.RVAL", DBR_LONG, 40);
#else
    testSkip(12, "No multilock");
#endif
}

void testSingleMonitor(const PDBProvider::shared_pointer& prov)
{
    testDiag("test single monitor");

    testdbPutFieldOk("rec1", DBR_DOUBLE, 1.0);

    testDiag("subscribe to rec1.VAL");
    PVMonitor mon(prov, "rec1");
    mon.mon->start();

    testOk1(mon.monreq->waitForEvent());
    testDiag("Initial event");

    pva::MonitorElement::Ref e(mon.mon);

    testOk1(!!e);
    testOk1(!!e && e->changedBitSet->get(0));
    testFieldEqual<pvd::PVDouble>(e->pvStructurePtr, "value", 1.0);
    testFieldEqual<pvd::PVDouble>(e->pvStructurePtr, "display.limitHigh", 100.0);
    testFieldEqual<pvd::PVDouble>(e->pvStructurePtr, "display.limitLow", -100.0);

    testOk1(!e.next());

    testDiag("trigger new VALUE event");
    testdbPutFieldOk("rec1", DBR_DOUBLE, 11.0);

    testDiag("Wait for event");
    mon.monreq->waitForEvent();

    testOk1(!!e.next());
    if(!!e) testEqual(toString(*e->changedBitSet), "{1, 3, 4, 7, 8}");
    else testFail("oops");
    testFieldEqual<pvd::PVDouble>(e->pvStructurePtr, "value", 11.0);

    testOk1(!e.next());

    testDiag("trigger new PROPERTY event");
    testdbPutFieldOk("rec1.HOPR", DBR_DOUBLE, 50.0);

    testDiag("Wait for event");
    mon.monreq->waitForEvent();

    testOk1(!!e.next());
    if(!!e) testEqual(toString(*e->changedBitSet), "{7, 8, 11, 12, 15, 17, 18}");
    else testFail("oops");
    testFieldEqual<pvd::PVDouble>(e->pvStructurePtr, "display.limitHigh", 50.0);

    testOk1(!e.next());
}

void testGroupMonitor(const PDBProvider::shared_pointer& prov)
{
    testDiag("test group monitor");
#ifdef USE_MULTILOCK

    testdbPutFieldOk("rec3", DBR_DOUBLE, 3.0);
    testdbPutFieldOk("rec4", DBR_DOUBLE, 4.0);
    testdbPutFieldOk("rec3.RVAL", DBR_LONG, 30);
    testdbPutFieldOk("rec4.RVAL", DBR_LONG, 40);

    testDiag("subscribe to grp1");
    PVMonitor mon(prov, "grp1");
    pva::MonitorElement::Ref e(mon.mon);

    testOk1(mon.mon->start().isOK());

    testDiag("Wait for initial event");
    testOk1(mon.monreq->waitForEvent());
    testDiag("Initial event");

    testOk1(!!e.next());

    testOk1(!!e && e->changedBitSet->get(0));
    testFieldEqual<pvd::PVDouble>(e->pvStructurePtr, "fld1.value", 3.0);
    testFieldEqual<pvd::PVInt>(e->pvStructurePtr,    "fld2.value", 30);
    testFieldEqual<pvd::PVDouble>(e->pvStructurePtr, "fld3.value", 4.0);
    testFieldEqual<pvd::PVInt>(e->pvStructurePtr,    "fld4.value", 40);
    testFieldEqual<pvd::PVDouble>(e->pvStructurePtr, "fld1.display.limitHigh", 200.0);
    testFieldEqual<pvd::PVDouble>(e->pvStructurePtr, "fld1.display.limitLow", -200.0);
    testFieldEqual<pvd::PVDouble>(e->pvStructurePtr, "fld2.display.limitHigh", 2147483647.0);
    testFieldEqual<pvd::PVDouble>(e->pvStructurePtr, "fld2.display.limitLow", -2147483648.0);

    testOk1(!e.next());

    testdbPutFieldOk("rec3", DBR_DOUBLE, 32.0);

    testDiag("Wait for event");
    testOk1(mon.monreq->waitForEvent());
    testDiag("event");

    testOk1(!!e.next());
    testOk1(!!e && e->pvStructurePtr->getSubFieldT("fld1.value")->getFieldOffset()==6);
    if(!!e) testEqual(toString(*e->changedBitSet), "{6, 8, 9, 12, 13}");
    else testFail("oops");

    testFieldEqual<pvd::PVDouble>(e->pvStructurePtr, "fld1.value", 32.0);
#else
    testSkip(23, "No multilock");
#endif
}

void testGroupMonitorTriggers(const PDBProvider::shared_pointer& prov)
{
    testDiag("test group monitor w/ triggers");
#ifdef USE_MULTILOCK

    testdbPutFieldOk("rec5", DBR_DOUBLE, 5.0);
    testdbPutFieldOk("rec6", DBR_DOUBLE, 6.0);
    testdbPutFieldOk("rec5.RVAL", DBR_LONG, 50);

    testDiag("subscribe to grp2");
    PVMonitor mon(prov, "grp2");
    pva::MonitorElement::Ref e(mon.mon);

    testOk1(mon.mon->start().isOK());

    testDiag("Wait for initial event");
    testOk1(mon.monreq->waitForEvent());
    testDiag("Initial event");

    testOk1(!!e.next());

    testOk1(!!e && e->changedBitSet->get(0));

    testFieldEqual<pvd::PVDouble>(e->pvStructurePtr, "fld1.value", 5.0);
    testFieldEqual<pvd::PVDouble>(e->pvStructurePtr, "fld2.value", 6.0);
    testFieldEqual<pvd::PVInt>(e->pvStructurePtr,    "fld3.value", 0); // not triggered -> no update.  only get/set

    testOk1(!e.next());

    testdbPutFieldOk("rec5.RVAL", DBR_LONG, 60); // no trigger -> no event
    testdbPutFieldOk("rec5", DBR_DOUBLE, 15.0); // no trigger -> no event
    testdbPutFieldOk("rec6", DBR_DOUBLE, 16.0); // event triggered

    testDiag("Wait for event");
    testOk1(mon.monreq->waitForEvent());
    testDiag("event");

    testOk1(!!e.next());

    testOk1(!!e && e->pvStructurePtr->getSubFieldT("fld1.value")->getFieldOffset()==6);
    testOk1(!!e && e->pvStructurePtr->getSubFieldT("fld2.value")->getFieldOffset()==46);
    if(!!e) testEqual(toString(*e->changedBitSet), "{6, 8, 9, 12, 13, 46, 48, 49, 52, 53}");
    else testFail("oops");

    testFieldEqual<pvd::PVDouble>(e->pvStructurePtr, "fld1.value", 15.0);
    testFieldEqual<pvd::PVDouble>(e->pvStructurePtr, "fld2.value", 16.0);
    testFieldEqual<pvd::PVInt>(e->pvStructurePtr,    "fld3.value", 0); // not triggered -> no update.  only get/set

    testOk1(!e.next());
#else
    testSkip(23, "No multilock");
#endif
}

} // namespace

extern "C"
void p2pTestIoc_registerRecordDeviceDriver(struct dbBase *);

MAIN(testpdb)
{
    testPlan(140);
    try{
        TestIOC IOC;

        testdbReadDatabase("p2pTestIoc.dbd", NULL, NULL);
        p2pTestIoc_registerRecordDeviceDriver(pdbbase);
        testdbReadDatabase("testpdb.db", NULL, NULL);

        IOC.init();

        PDBProvider::shared_pointer prov(new PDBProvider());
        try {
            testSingleGet(prov);
            testGroupGet(prov);

            testSinglePut(prov);
            testGroupPut(prov);

            testSingleMonitor(prov);
            testGroupMonitor(prov);
            testGroupMonitorTriggers(prov);
        }catch(...){
            prov->destroy();
            throw;
        }
        prov->destroy();
        testOk1(prov.unique());
        prov.reset();

        iocshCmd("stopPVAServer");

        testDiag("check to see that all dbChannel are closed before IOC shuts down");
        testEqual(epics::atomic::get(PDBProvider::num_instances), 0u);
#ifdef USE_MULTILOCK
        testEqual(epics::atomic::get(PDBGroupChannel::num_instances), 0u);
        testEqual(epics::atomic::get(PDBGroupPV::num_instances), 0u);
#else
        testSkip(2, "No multilock");
#endif // USE_MULTILOCK
        testEqual(epics::atomic::get(PDBSinglePV::num_instances), 0u);

    }catch(std::exception& e){
        PRINT_EXCEPTION(e);
        testAbort("Unexpected Exception: %s", e.what());
    }
    return testDone();
}
