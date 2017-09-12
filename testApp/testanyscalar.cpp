
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

void test_ctor()
{
    testDiag("test_ctor()");
    AnyScalar A(10),
              B(10.0),
              C("foo"),
              D(std::string("bar"));

    testEqual(A.type(), pvd::pvInt);
    testEqual(B.type(), pvd::pvDouble);
    testEqual(C.type(), pvd::pvString);
    testEqual(D.type(), pvd::pvString);

    testEqual(A.ref<pvd::int32>(), 10);
    testEqual(B.ref<double>(), 10);
    testEqual(C.ref<std::string>(), "foo");
    testEqual(D.ref<std::string>(), "bar");
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

void test_swap()
{
    testDiag("test_swap()");

    // AnyScalar::swap() has 3 cases each for LHS and RHS
    // nil, string, and non-string
    // So we have 9 cases to test

    {
        AnyScalar A, B;
        A.swap(B);
        testOk1(A.empty());
        testOk1(B.empty());
    }
    {
        AnyScalar A, B("hello");
        A.swap(B);
        testEqual(A.ref<std::string>(), "hello");
        testOk1(B.empty());
    }
    {
        AnyScalar A, B(40);
        A.swap(B);
        testEqual(A.ref<pvd::int32>(), 40);
        testOk1(B.empty());
    }

    {
        AnyScalar A("world"), B;
        A.swap(B);
        testOk1(A.empty());
        testEqual(B.ref<std::string>(), "world");
    }
    {
        AnyScalar A("world"), B("hello");
        A.swap(B);
        testEqual(A.ref<std::string>(), "hello");
        testEqual(B.ref<std::string>(), "world");
    }
    {
        AnyScalar A("world"), B(40);
        A.swap(B);
        testEqual(A.ref<pvd::int32>(), 40);
        testEqual(B.ref<std::string>(), "world");
    }

    {
        AnyScalar A(39), B;
        A.swap(B);
        testOk1(A.empty());
        testEqual(B.ref<pvd::int32>(), 39);
    }
    {
        AnyScalar A(39), B("hello");
        A.swap(B);
        testEqual(A.ref<std::string>(), "hello");
        testEqual(B.ref<pvd::int32>(), 39);
    }
    {
        AnyScalar A(39), B(40);
        A.swap(B);
        testEqual(A.ref<pvd::int32>(), 40);
        testEqual(B.ref<pvd::int32>(), 39);
    }
}

}

MAIN(testanyscalar)
{
    testPlan(54);
    try {
        test_empty();
        test_ctor();
        test_basic();
        test_swap();
    }catch(std::exception& e){
        testAbort("Unexpected exception: %s", e.what());
    }
    return testDone();
}
