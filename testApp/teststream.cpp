
#include <string.h>

#include <testMain.h>
#include <epicsUnitTest.h>
#include <epicsEvent.h>
#include <epicsThread.h>

#include "errlogstream.h"

namespace {

struct tester {
    std::string buf;
    epicsEvent evnt;
    bool wait;
    unsigned called;

    tester() :wait(false), called(0) {
        errlogAddListener(&handler, (void*)this);
    }
    ~tester() {
        errlogRemoveListeners(&handler, (void*)this);
    }

    static void handler(void *raw, const char *msg)
    {
        tester *self = (tester*)raw;
        if(self->wait)
            self->evnt.wait();
        self->called++;
        self->buf += msg;
    }
};

void testNoBlock(const char *msg)
{
    testDiag("Test non blocking with message length %u", (unsigned)strlen(msg));
    tester T;
    T.wait = true;

    {
        errlog_ostream strm(false, 16);

        strm<<msg;
    }
    testDiag("sleep");
    epicsThreadSleep(0.1);
    // see that stream dtor doesn't call errlogFlush()
    testOk(T.called==0, "called %u times", T.called);
    T.wait = false;
    T.evnt.signal();
    testDiag("flush");
    errlogFlush();
    testOk(T.called!=0, "called %u times", T.called);
    testOk(T.buf==msg, "\"%s\"==\"%s\"", T.buf.c_str(), msg);
}

void testBlock(const char *msg)
{
    testDiag("Test blocking with message length %u", (unsigned)strlen(msg));
    tester T;

    {
        errlog_ostream strm(true, 16);

        strm<<msg;
    }

    testOk(T.called!=0, "called %u times", T.called);
    testOk(T.buf==msg, "\"%s\"==\"%s\"", T.buf.c_str(), msg);
}

} // namespace

static const char longmsg[] = "This message should be longer than the mimimum buffer length"
                              " so that it results in a call to overflow()";

MAIN(teststream)
{
    testPlan(10);
    eltc(0);
    testNoBlock("hello");
    testNoBlock(longmsg);
    testBlock("hello");
    testBlock(longmsg);
    eltc(1);
    return testDone();
}
