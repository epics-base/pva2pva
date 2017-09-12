
#include <sstream>

#include <pv/pvIntrospect.h>

#include <pv/pvUnitTest.h>
#include <testMain.h>

#include "anyscalar.h"

namespace pvd = epics::pvData;

namespace {

void test_empty()
{
    testDiag("test_empty()");
    AnyScalar O;
    testOk1(O.empty());
    testOk1(!O);

    testThrows(AnyScalar::bad_cast, O.ref<double>());
    testThrows(AnyScalar::bad_cast, O.as<double>());
}

void test_basic()
{
    testDiag("test_basic()");
    AnyScalar I(42);

    testOk1(!I.empty());
    testOk1(!!I);

    testEqual(I.type(), pvd::pvInt);
    testEqual(I.ref<pvd::int32>(), 42);
    testEqual(I.as<pvd::int32>(), 42);
    testEqual(I.as<double>(), 42.0);
    testEqual(I.as<std::string>(), "42");

    testThrows(AnyScalar::bad_cast, I.ref<double>());

    {
        std::ostringstream strm;
        strm<<I;
        testEqual(strm.str(), "42");
    }

    I.ref<pvd::int32>() = 43;

    testEqual(I.ref<pvd::int32>(), 43);
    testEqual(I.as<pvd::int32>(), 43);
    testEqual(I.as<double>(), 43.0);

    I = AnyScalar("hello");

    testEqual(I.type(), pvd::pvString);
    testEqual(I.ref<std::string>(), "hello");
    testEqual(I.as<std::string>(), "hello");

    testThrows(AnyScalar::bad_cast, I.ref<pvd::int32>());

    {
        AnyScalar O(I);
        testOk1(!I.empty());
        testOk1(!O.empty());

        testEqual(I.ref<std::string>(), "hello");
        testEqual(O.ref<std::string>(), "hello");
    }

    {
        AnyScalar O;
        I.swap(O);
        testOk1(I.empty());
        testOk1(!O.empty());

        testThrows(AnyScalar::bad_cast, I.ref<std::string>());
        testEqual(O.ref<std::string>(), "hello");

        I.swap(O);
    }
}

}

MAIN(testanyscalar)
{
    testPlan(28);
    try {
        test_empty();
        test_basic();
    }catch(std::exception& e){
        testAbort("Unexpected exception: %s", e.what());
    }
    return testDone();
}
