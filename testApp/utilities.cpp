
#include <utilities.h>
#include <helper.h>

typedef epicsGuard<epicsMutex> Guard;
typedef epicsGuardRelease<epicsMutex> UnGuard;

namespace pvd = epics::pvData;
namespace pva = epics::pvAccess;

TestChannelRequester::TestChannelRequester()
    :laststate(pva::Channel::NEVER_CONNECTED)
{}

void TestChannelRequester::channelCreated(const pvd::Status& status, pva::Channel::shared_pointer const & channel)
{
    Guard G(lock);
    laststate = pva::Channel::CONNECTED;
    this->status = status;
    chan = channel;
    wait.trigger();
}

void TestChannelRequester::channelStateChange(pva::Channel::shared_pointer const & channel,
                                              pva::Channel::ConnectionState connectionState)
{
    Guard G(lock);
    laststate = connectionState;
    wait.trigger();
}

bool TestChannelRequester::waitForConnect()
{
    Guard G(lock);
    assert(chan);
    while(true) {
        pva::Channel::ConnectionState cur = chan->getConnectionState();
        switch(cur) {
        case pva::Channel::NEVER_CONNECTED:
            break;
        case pva::Channel::CONNECTED:
            return true;
        case pva::Channel::DISCONNECTED:
        case pva::Channel::DESTROYED:
            return false;
        }
        UnGuard U(G);
        wait.wait();
    }

}

TestChannelMonitorRequester::TestChannelMonitorRequester()
    :connected(false)
    ,unlistend(false)
    ,eventCnt(0)
{}

void TestChannelMonitorRequester::monitorConnect(pvd::Status const & status,
                                                 pvd::MonitorPtr const & monitor,
                                                 pvd::StructureConstPtr const & structure)
{
    Guard G(lock);
    connectStatus = status;
    dtype = structure;
    connected = true;
    wait.trigger();
}

void TestChannelMonitorRequester::monitorEvent(pvd::MonitorPtr const & monitor)
{
    mon = monitor;
    eventCnt++;
    wait.trigger();
}

void TestChannelMonitorRequester::unlisten(pvd::MonitorPtr const & monitor)
{
    Guard G(lock);
    unlistend = true;
    wait.trigger();
}

bool TestChannelMonitorRequester::waitForEvent()
{
    Guard G(lock);
    size_t icnt = eventCnt;
    while(!unlistend && eventCnt==icnt) {
        UnGuard U(G);
        wait.wait();
    }
    return !unlistend;
}

TestPVChannel::TestPVChannel(const std::tr1::shared_ptr<TestPV> &pv,
                             const std::tr1::shared_ptr<pva::ChannelRequester> &req)
    :pv(pv)
    ,requester(req)
    ,state(CONNECTED)
{}

TestPVChannel::~TestPVChannel()
{
    Guard G(pv->provider->lock);
    if(requester)
        testDiag("Warning: TestPVChannel dropped w/o destroy()");
}

void TestPVChannel::destroy()
{
    std::tr1::shared_ptr<TestProvider> P(pv->provider);
    Guard G(P->lock);
    requester.reset();
    state = DESTROYED;
}

std::tr1::shared_ptr<pva::ChannelProvider>
TestPVChannel::getProvider()
{ return pv->provider; }

TestPVChannel::ConnectionState TestPVChannel::getConnectionState()
{
    Guard G(pv->provider->lock);
    return state;
}

std::string TestPVChannel::getChannelName()
{ return pv->name; }

bool TestPVChannel::isConnected()
{
    return getConnectionState()==CONNECTED;
}

void TestPVChannel::getField(pva::GetFieldRequester::shared_pointer const & requester,std::string const & subField)
{
    Guard G(pv->provider->lock);

    //TODO subField?
    requester->getDone(pvd::Status(), pv->dtype);
}

pva::ChannelProcess::shared_pointer
TestPVChannel::createChannelProcess(
        pva::ChannelProcessRequester::shared_pointer const & requester,
        pvd::PVStructure::shared_pointer const & pvRequest)
{
    pva::ChannelProcess::shared_pointer ret;
    requester->channelProcessConnect(pvd::Status(pvd::Status::STATUSTYPE_FATAL, "Not implemented"), ret);
    return ret;
}

pva::ChannelGet::shared_pointer
TestPVChannel::createChannelGet(
        pva::ChannelGetRequester::shared_pointer const & requester,
        pvd::PVStructure::shared_pointer const & pvRequest)
{
    pva::ChannelGet::shared_pointer ret;
    requester->channelGetConnect(pvd::Status(pvd::Status::STATUSTYPE_FATAL, "Not implemented"),
                       ret,
                       pvd::StructureConstPtr());
    return ret;
}

pva::ChannelPut::shared_pointer
TestPVChannel::createChannelPut(
        pva::ChannelPutRequester::shared_pointer const & requester,
        pvd::PVStructure::shared_pointer const & pvRequest)
{
    pva::ChannelPut::shared_pointer ret;
    requester->channelPutConnect(pvd::Status(pvd::Status::STATUSTYPE_FATAL, "Not implemented"),
                                 ret,
                                 pvd::StructureConstPtr());
    return ret;
}

pva::ChannelPutGet::shared_pointer
TestPVChannel::createChannelPutGet(
        pva::ChannelPutGetRequester::shared_pointer const & requester,
        pvd::PVStructure::shared_pointer const & pvRequest)
{
    pva::ChannelPutGet::shared_pointer ret;
    requester->channelPutGetConnect(pvd::Status(pvd::Status::STATUSTYPE_FATAL, "Not implemented"),
                                    ret,
                                    pvd::StructureConstPtr(),
                                    pvd::StructureConstPtr());
    return ret;
}

pva::ChannelRPC::shared_pointer
TestPVChannel::createChannelRPC(
        pva::ChannelRPCRequester::shared_pointer const & requester,
        pvd::PVStructure::shared_pointer const & pvRequest)
{
    pva::ChannelRPC::shared_pointer ret;
    requester->channelRPCConnect(pvd::Status(pvd::Status::STATUSTYPE_FATAL, "Not implemented"),
                                 ret);
    return ret;
}

pvd::Monitor::shared_pointer
TestPVChannel::createMonitor(
        pvd::MonitorRequester::shared_pointer const & requester,
        pvd::PVStructure::shared_pointer const & pvRequest)
{
    shared_pointer self(weakself);
    TestPVMonitor::shared_pointer ret(new TestPVMonitor(self, requester, 2));
    {
        Guard G(pv->provider->lock);
        monitors.insert(ret);
        static_cast<TestPVMonitor*>(ret.get())->weakself = ret; // save wrapped weak ref
    }
    requester->monitorConnect(pvd::Status(), ret, pv->dtype);
    return ret;
}

pva::ChannelArray::shared_pointer
TestPVChannel::createChannelArray(
        pva::ChannelArrayRequester::shared_pointer const & requester,
        pvd::PVStructure::shared_pointer const & pvRequest)
{
    pva::ChannelArray::shared_pointer ret;
    requester->channelArrayConnect(pvd::Status(pvd::Status::STATUSTYPE_FATAL, "Not implemented"),
                                   ret,
                                   pvd::Array::const_shared_pointer());
    return ret;
}

TestPVMonitor::TestPVMonitor(const TestPVChannel::shared_pointer& ch,
              const pvd::MonitorRequester::shared_pointer& req,
              size_t bsize)
    :channel(ch)
    ,requester(req)
    ,running(false)
    ,finalize(false)
    ,inoverflow(false)
    ,needWakeup(false)
{
    pvd::PVDataCreatePtr fact(pvd::PVDataCreate::getPVDataCreate());
    for(size_t i=0; i<bsize; i++) {
        pvd::MonitorElementPtr elem(new pvd::MonitorElement(fact->createPVStructure(channel->pv->dtype)));
        free.push_back(elem);
    }
}

TestPVMonitor::~TestPVMonitor()
{
    Guard G(channel->pv->provider->lock);
    if(requester)
        testDiag("Warning: TestPVMonitor dropped w/o destroy()");
}

void TestPVMonitor::destroy()
{
    Guard G(channel->pv->provider->lock);
    requester.reset();

    shared_pointer self(weakself);
    channel->monitors.erase(self); // ensure we don't get more notifications
}

pvd::Status TestPVMonitor::start()
{
    bool wake;
    {
        Guard G(channel->pv->provider->lock);
        if(finalize && buffer.empty())
            return pvd::Status();

        if(!running) {
            wake = running = needWakeup = true;
        }

        if(!this->free.empty()) {
            pvd::MonitorElementPtr monitorElement(this->free.front());

            if(changedMask.isEmpty()) {
                changedMask.set(0); // initial update has all changed
                overflowMask.clear();
            }

            monitorElement->pvStructurePtr->copyUnchecked(*channel->pv->value);
            *monitorElement->changedBitSet = changedMask;
            *monitorElement->overrunBitSet = overflowMask;
            changedMask.clear();
            overflowMask.clear();

            buffer.push_back(monitorElement);
            this->free.pop_front();
        }
    }
    (void)wake; // todo notify?
    return pvd::Status();
}

pvd::Status TestPVMonitor::stop()
{
    Guard G(channel->pv->provider->lock);
    running = false;
    return pvd::Status();
}

pvd::MonitorElementPtr TestPVMonitor::poll()
{
    pvd::MonitorElementPtr ret;
    Guard G(channel->pv->provider->lock);
    if(!buffer.empty()) {
        ret = buffer.front();
        buffer.pop_front();
    }
    testDiag("pop %p", ret.get());
    return ret;
}

void TestPVMonitor::release(pvd::MonitorElementPtr const & monitorElement)
{
    Guard G(channel->pv->provider->lock);
    testDiag("release %p", monitorElement.get());

    if(inoverflow) {
        assert(!buffer.empty());
        assert(this->free.empty());

        monitorElement->pvStructurePtr->copyUnchecked(*channel->pv->value);
        *monitorElement->changedBitSet = changedMask;
        *monitorElement->overrunBitSet = overflowMask;

        changedMask.clear();
        overflowMask.clear();

        buffer.push_back(monitorElement);
        testDiag("overflow resume %p", monitorElement.get());
        inoverflow = false;
    } else {
        this->free.push_back(monitorElement);
    }
}

TestPV::TestPV(const std::string& name,
               const std::tr1::shared_ptr<TestProvider>& provider,
               const pvd::StructureConstPtr& dtype)
    :name(name)
    ,provider(provider)
    ,factory(pvd::PVDataCreate::getPVDataCreate())
    ,dtype(dtype)
    ,value(factory->createPVStructure(dtype))
{}

void TestPV::post(const pvd::BitSet& changed, bool notify)
{
    Guard G(provider->lock);
    channels_t::vector_type toupdate(channels.lock_vector());

    FOREACH(it, end, toupdate) // channel
    {
        TestPVChannel *chan = it->get();

        TestPVChannel::monitors_t::vector_type tomon(chan->monitors.lock_vector());
        FOREACH(it2, end2, tomon) // monitor/subscription
        {
            TestPVMonitor *mon = it2->get();

            if(mon->inoverflow || mon->free.empty()) {
                mon->inoverflow = true;
                mon->overflowMask.or_and(mon->changedMask, changed); // oflow = prev_changed & new_changed
                mon->changedMask |= changed;
                testDiag("overflow");

            } else {

                if(!mon->needWakeup)
                    mon->needWakeup = mon->buffer.empty();

                AUTO_REF(elem, mon->free.front());
                elem->pvStructurePtr->copyUnchecked(*value);
                *elem->changedBitSet = changed;
                elem->overrunBitSet->clear(); // redundant/paranoia

                mon->buffer.push_back(elem);
                mon->free.pop_front();
                testDiag("push %p", elem.get());
            }

            if(mon->needWakeup && notify) {
                UnGuard U(G);
                mon->requester->monitorEvent(*it2);
            }
        }
    }
}

void TestPV::disconnect()
{
    Guard G(provider->lock);
    channels_t::vector_type toupdate(channels.lock_vector());

    FOREACH(it, end, toupdate) // channel
    {
        TestPVChannel *chan = it->get();

        chan->state = TestPVChannel::DISCONNECTED;
        {
            UnGuard U(G);
            chan->requester->channelStateChange(*it, TestPVChannel::DISCONNECTED);
        }
    }
}

TestProvider::TestProvider() {}

void TestProvider::destroy()
{
    // TODO: disconnect all?
}

pva::ChannelFind::shared_pointer
TestProvider::channelList(pva::ChannelListRequester::shared_pointer const & requester)
{
    pva::ChannelFind::shared_pointer ret;
    pvd::PVStringArray::const_svector names;
    requester->channelListResult(pvd::Status(pvd::Status::STATUSTYPE_FATAL, "Not implemented"),
                                 ret,
                                 names,
                                 true);
    return ret;
}

pva::ChannelFind::shared_pointer
TestProvider::channelFind(std::string const & channelName,
                          pva::ChannelFindRequester::shared_pointer const & requester)
{
    pva::ChannelFind::shared_pointer ret;
    requester->channelFindResult(pvd::Status(pvd::Status::STATUSTYPE_FATAL, "Not implemented"),
                                 ret, false);
    return ret;
}

pva::Channel::shared_pointer
TestProvider::createChannel(std::string const & channelName,pva::ChannelRequester::shared_pointer const & requester,
                                       short priority)
{
    return createChannel(channelName, requester, priority, "<unused>");
}

pva::Channel::shared_pointer
TestProvider::createChannel(std::string const & channelName,
                            pva::ChannelRequester::shared_pointer const & requester,
                            short priority, std::string const & address)
{
    pva::Channel::shared_pointer ret;

    {
        Guard G(lock);

        TestPV::shared_pointer pv(pvs.find(channelName));
        if(pv) {
            TestPVChannel::shared_pointer chan(new TestPVChannel(pv, requester));
            pv->channels.insert(chan);
            chan->weakself = chan;
            ret = chan;
        }
    }

    if(ret) {
        requester->channelCreated(pvd::Status(), ret);
    } else {
        requester->channelCreated(pvd::Status(pvd::Status::STATUSTYPE_ERROR, "PV not found"), ret);
    }
    return ret;
}

TestPV::shared_pointer
TestProvider::addPV(const std::string& name, const pvd::StructureConstPtr& tdef)
{
    Guard G(lock);
    TestPV::shared_pointer ret(new TestPV(name, shared_from_this(), tdef));
    pvs.insert(name, ret);
    return ret;
}
