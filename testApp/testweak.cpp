
#include "weakset.h"
#include "weakmap.h"

#include <epicsUnitTest.h>
#include <testMain.h>

namespace {

static
void testWeakSet1()
{
    testDiag("Test1 weak_set");

    weak_set<int>::value_pointer ptr;
    weak_set<int> set;

    testOk1(set.empty());

    ptr.reset(new int(5));
    set.insert(ptr);

    set.insert(ptr); // second insert is a no-op

    testOk1(ptr.unique()); // we hold the only "fake" strong ref.

    {
        weak_set<int>::set_type S(set.lock_set());
        testOk1(!S.empty());
        testOk1(S.size()==1);
        testOk1(S.find(ptr)!=S.end());
    }
    {
        weak_set<int>::vector_type S(set.lock_vector());
        testOk1(!S.empty());
        testOk1(S.size()==1);
        testOk1(*S[0]==5);
    }
}

static
void testWeakSet2()
{
    testDiag("Test2 weak_set");

    weak_set<int> set;
    weak_set<int>::value_pointer ptr;

    testOk1(set.empty());

    ptr.reset(new int(5));
    set.insert(ptr);

    testOk1(!set.empty());

    testOk1(ptr.unique());
    ptr.reset(); // implicitly removes from set

    testOk1(set.empty());

    ptr.reset(new int(5));
    set.insert(ptr);

    set.clear();
    testOk1(set.empty());
    testOk1(!!ptr);
}

static
void testWeakSetInvalid()
{
    testDiag("Test adding non-unique");
    weak_set<int> set;
    weak_set<int>::value_pointer ptr(new int(5)),
            other(ptr);

    testOk1(!ptr.unique());

    try{
        set.insert(ptr);
        testFail("Missed expected exception");
    } catch(std::invalid_argument& e) {
        testPass("Got expected exception: %s", e.what());
    }
}

static
void testWeakMap1()
{
    testDiag("Test weak_value_map1");

    weak_set<int>::value_pointer ptr;
    weak_value_map<int,int> map;

    testOk1(map.empty());

    ptr.reset(new int(5));
    map[4] = ptr;

    testOk1(!map.empty());
    {
        weak_value_map<int,int>::lock_vector_type V(map.lock_vector());
        testOk1(V.size()==1);
        testOk1(V[0].first==4);
        testOk1(*V[0].second==5);
    }

    testOk1(map[4]==ptr);
    testOk1(*map[4]==5);
}

static
void testWeakMap2()
{
    testDiag("Test weak_value_map2");

    weak_set<int>::value_pointer ptr;
    weak_value_map<int,int> map;

    testOk1(map.empty());

    ptr.reset(new int(5));
    map[4] = ptr;

    testOk1(!map.empty());
    {
        weak_value_map<int,int>::lock_vector_type V(map.lock_vector());
        testOk1(V.size()==1);
        testOk1(V[0].first==4);
        testOk1(*V[0].second==5);
    }

    ptr.reset();
    testOk1(map.empty());

    ptr.reset(new int(5));
    map[4] = ptr;
    {
        weak_set<int>::value_pointer O(map[4]);
        testOk1(O==ptr);
    }

    testOk1(map.size()==1);
    map.clear();
    testOk1(map.empty());
    testOk1(!!ptr);
}

} // namespace

MAIN(testweak)
{
    testPlan(33);
    testWeakSet1();
    testWeakSet2();
    testWeakSetInvalid();
    testWeakMap1();
    testWeakMap2();
    return testDone();
}
