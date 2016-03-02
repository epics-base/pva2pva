
#include <testMain.h>

#include <dbAccess.h>

#include <pv/epicsException.h>

#include "utilities.h"
#include "pdb.h"

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
        std::cout<<"# expect='"<<expect<<"' actual='"<<actual<<"'\n";
        testOk1(actual==expect);
    }
}

pvd::PVStructurePtr pvget(const pva::ChannelProvider::shared_pointer& prov, const char *name,
                          bool atomic)
{
    pvd::StructureConstPtr def(pvd::getFieldCreate()->createFieldBuilder()
                               ->addNestedStructure("record")
                                   ->addNestedStructure("_options")
                                       ->add("atomic", pvd::pvBoolean)
                                       ->endNested()
                                   ->endNested()
                               ->createStructure());
    pvd::PVStructurePtr pvr(pvd::getPVDataCreate()->createPVStructure(def));
    pvr->getSubFieldT<pvd::PVBoolean>("record._options.atomic")->put(atomic);


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
    return greq->value;
}

void testSingle(const PDBProvider::shared_pointer& prov)
{
    pvd::PVStructurePtr value;

    value = pvget(prov, "rec1", false);
    testFieldEqual<pvd::PVDouble>(value, "value", 1.0);

    value = pvget(prov, "rec1.RVAL", false);
    testFieldEqual<pvd::PVInt>(value, "value", 10);
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
        testSingle(prov);

    }catch(std::exception& e){
        PRINT_EXCEPTION(e);
        testAbort("Unexpected Exception: %s", e.what());
    }
    return testDone();
}
