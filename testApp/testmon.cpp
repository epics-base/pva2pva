
#include <epicsGuard.h>
#include <epicsUnitTest.h>
#include <testMain.h>

#include <pv/monitor.h>
#include <pv/thread.h>

#include "utilities.h"

namespace pvd = epics::pvData;

typedef epicsGuard<epicsMutex> Guard;

namespace {

struct DownStreamReq : public pvd::MonitorRequester, DumbRequester
{
    pvd::Mutex lock;
    bool connected;
    bool unlistend;
    size_t eventCnt;
    pvd::Status connectStatus;
    pvd::MonitorPtr mon;
    pvd::StructureConstPtr structure;

    DownStreamReq()
        :DumbRequester("DownStreamReq")
        ,connected(false)
        ,unlistend(false)
        ,eventCnt(0)
    {}

    virtual void monitorConnect(pvd::Status const & status,
        pvd::MonitorPtr const & monitor, pvd::StructureConstPtr const & structure)
    {
        Guard G(lock);
        assert(!connected);
        connectStatus = status;
        mon = monitor;
        this->structure = structure;
        connected = true;
    }

    virtual void monitorEvent(MonitorPtr const & monitor)
    {
        Guard G(lock);
        mon = monitor;
        eventCnt++;
    }

    virtual void unlisten(MonitorPtr const & monitor)
    {
        Guard G(lock);
        assert(!unlistend);
        unlistend = true;
    }
};

} // namespace

MAIN(testmon)
{
    testPlan(0);
    return testDone();
}
