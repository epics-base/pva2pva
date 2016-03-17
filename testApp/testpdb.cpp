
#include <testMain.h>

#include <epicsAtomic.h>
#include <dbAccess.h>

#include <pv/epicsException.h>

#include "utilities.h"
#include "pdb.h"
#include "pdbgroup.h"
#include "pdbsingle.h"

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

    struct Element {
        pva::MonitorElementPtr elem;
        pva::Monitor::shared_pointer mon;

        Element(const PVMonitor& m) : mon(m.mon) {}
        ~Element() {
            if(elem) mon->release(elem);
        }
        Element& operator=(const pva::MonitorElementPtr& e) {
            if(elem) mon->release(elem);
            elem = e;
            return *this;
        }

        pvd::BitSet& changed() { return *elem->changedBitSet; }
        pvd::BitSet& overflow() { return *elem->overrunBitSet; }
        pvd::PVStructure* operator->() { return elem->pvStructurePtr.get(); }
        operator pvd::PVStructurePtr&() { return elem->pvStructurePtr; }
        bool operator!() const { return !elem; }
    private:
        Element(const Element& e);
        Element& operator=(const Element& e);
    };

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

    testdbPutFieldOk("rec3", DBR_DOUBLE, 3.0);
    testdbPutFieldOk("rec4", DBR_DOUBLE, 4.0);
    testdbPutFieldOk("rec3.RVAL", DBR_LONG, 30);
    testdbPutFieldOk("rec4.RVAL", DBR_LONG, 40);

    PVPut put(prov, "grp1");

    pvd::PVDoublePtr val(put.putval->getSubFieldT<pvd::PVDouble>("fld1.value"));
    val->put(2.0);
    put.putchanged->clear();
    put.put();

    testdbGetFieldEqual("rec3", DBR_DOUBLE, 3.0);
    testdbGetFieldEqual("rec4", DBR_DOUBLE, 4.0);
    testdbGetFieldEqual("rec3.RVAL", DBR_LONG, 30);
    testdbGetFieldEqual("rec4.RVAL", DBR_LONG, 40);

    put.putchanged->set(val->getFieldOffset());
    val = put.putval->getSubFieldT<pvd::PVDouble>("fld3.value");
    val->put(5.0);
    put.putchanged->set(val->getFieldOffset());
    put.put();

    testdbGetFieldEqual("rec3", DBR_DOUBLE, 2.0);
    testdbGetFieldEqual("rec4", DBR_DOUBLE, 5.0);
    testdbGetFieldEqual("rec3.RVAL", DBR_LONG, 30);
    testdbGetFieldEqual("rec4.RVAL", DBR_LONG, 40);
}

void testSingleMonitor(const PDBProvider::shared_pointer& prov)
{
    testDiag("test single monitor");

    testdbPutFieldOk("rec1", DBR_DOUBLE, 1.0);

    testDiag("subscribe to rec1.VAL");
    PVMonitor mon(prov, "rec1");
    mon.mon->start();

    // start() will trigger two updates, one for DBE_VALUE|DBE_ALARM
    // and another for DBE_PROPERTY
    testOk1(mon.monreq->waitForEvent());
    testDiag("Initial event");

    PVMonitor::Element e(mon);

    // TODO: correctly check the first update
    // no mather which DBE_* arrives first...
    e = mon.poll();
    testOk1(!!e);
    while(!(e = mon.poll())) {
        testDiag("Wait initial event (part 2)");
        mon.monreq->waitForEvent();
    }

    testOk1(!!e);
    testFieldEqual<pvd::PVDouble>(e, "value", 1.0);
    testFieldEqual<pvd::PVDouble>(e, "display.limitHigh", 100.0);
    testFieldEqual<pvd::PVDouble>(e, "display.limitLow", -100.0);

    e = mon.poll();
    testOk1(!e);

    testDiag("trigger new VALUE event");
    testdbPutFieldOk("rec1", DBR_DOUBLE, 11.0);

    testDiag("Wait for event");
    mon.monreq->waitForEvent();

    e = mon.poll();
    testOk1(!!e);
    testFieldEqual<pvd::PVDouble>(e, "value", 11.0);

    e = mon.poll();
    testOk1(!e);

    testDiag("trigger new PROPERTY event");
    testdbPutFieldOk("rec1.HOPR", DBR_DOUBLE, 50.0);

    testDiag("Wait for event");
    mon.monreq->waitForEvent();

    e = mon.poll();
    testOk1(!!e);
    testFieldEqual<pvd::PVDouble>(e, "display.limitHigh", 50.0);

    e = mon.poll();
    testOk1(!e);
}

void testGroupMonitor(const PDBProvider::shared_pointer& prov)
{
    testDiag("test group monitor");

    testdbPutFieldOk("rec3", DBR_DOUBLE, 3.0);
    testdbPutFieldOk("rec4", DBR_DOUBLE, 4.0);
    testdbPutFieldOk("rec3.RVAL", DBR_LONG, 30);
    testdbPutFieldOk("rec4.RVAL", DBR_LONG, 40);

    testDiag("subscribe to grp1");
    PVMonitor mon(prov, "grp1");
    PVMonitor::Element e(mon);

    testOk1(mon.mon->start().isOK());

    testDiag("Wait for initial event");
    testOk1(mon.monreq->waitForEvent());
    testDiag("Initial event");

    e = mon.poll();
    testOk1(!!e);

    testFieldEqual<pvd::PVDouble>(e, "fld1.value", 3.0);
    testFieldEqual<pvd::PVInt>(e,    "fld2.value", 30);
    testFieldEqual<pvd::PVDouble>(e, "fld3.value", 4.0);
    testFieldEqual<pvd::PVInt>(e,    "fld4.value", 40);
    testFieldEqual<pvd::PVDouble>(e, "fld1.display.limitHigh", 200.0);
    testFieldEqual<pvd::PVDouble>(e, "fld1.display.limitLow", -200.0);
    testFieldEqual<pvd::PVDouble>(e, "fld2.display.limitHigh", 2147483647.0);
    testFieldEqual<pvd::PVDouble>(e, "fld2.display.limitLow", -2147483648.0);
}

} // namespace

extern "C"
void p2pTestIoc_registerRecordDeviceDriver(struct dbBase *);

MAIN(testpdb)
{
    testPlan(0);
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
        }catch(...){
            prov->destroy();
            throw;
        }
        prov->destroy();
        prov.reset();

        testDiag("check to see that all dbChannel are closed before IOC shuts down");
        testEqual(epics::atomic::get(PDBGroupPV::ninstances), 0u);
        testEqual(epics::atomic::get(PDBSinglePV::ninstances), 0u);

    }catch(std::exception& e){
        PRINT_EXCEPTION(e);
        testAbort("Unexpected Exception: %s", e.what());
    }
    return testDone();
}
