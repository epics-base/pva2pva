
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

template<typename PVD>
void testFieldEqual(const pvd::PVStructurePtr& val, const char *name, typename PVD::value_type expect)
{
    typename PVD::shared_pointer fval(val->getSubField<PVD>(name));
    if(!fval) {
        testFail("field '%s' with type %s does not exist", name, typeid(PVD).name());
    } else {
        typename PVD::value_type actual(fval->get());
        testEqualx(name, "expect", actual, expect);
    }
}

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

struct PVPut
{
    TestChannelRequester::shared_pointer chreq;
    pva::Channel::shared_pointer chan;
    TestChannelFieldRequester::shared_pointer fldreq;
    TestChannelPutRequester::shared_pointer putreq;
    pva::ChannelPut::shared_pointer chput;
    pvd::PVStructurePtr putval;
    pvd::BitSetPtr putchanged;

    PVPut(const pva::ChannelProvider::shared_pointer& prov, const char *name, bool atomic=true)
        :chreq(new TestChannelRequester())
        ,chan(prov->createChannel(name, chreq))
        ,fldreq(new TestChannelFieldRequester())
        ,putreq(new TestChannelPutRequester())
    {
        if(!chan || !chan->isConnected())
            throw std::runtime_error("channel not connected");
        chan->getField(fldreq, "");
        if(!fldreq->done || !fldreq->fielddesc || fldreq->fielddesc->getType()!=pvd::structure)
            throw std::runtime_error("Failed to get fielddesc");
        putval = pvd::getPVDataCreate()->createPVStructure(std::tr1::static_pointer_cast<const pvd::Structure>(fldreq->fielddesc));
        pvd::PVStructurePtr pvr(makeRequest(atomic));
        chput = chan->createChannelPut(putreq, pvr);
        if(!chput)
            throw std::runtime_error("Failed to create put op");
        putchanged.reset(new pvd::BitSet());
    }
    ~PVPut() {
        chput->destroy();
        chan->destroy();
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

        {
            PDBProvider::shared_pointer prov(new PDBProvider());
            testSingleGet(prov);
            testGroupGet(prov);

            testSinglePut(prov);
            testGroupPut(prov);
        }
        testDiag("check to see that all dbChannel are closed before IOC shuts down");
        testEqual(epics::atomic::get(PDBGroupPV::ninstances), 0u);
        testEqual(epics::atomic::get(PDBSinglePV::ninstances), 0u);

    }catch(std::exception& e){
        PRINT_EXCEPTION(e);
        testAbort("Unexpected Exception: %s", e.what());
    }
    return testDone();
}
