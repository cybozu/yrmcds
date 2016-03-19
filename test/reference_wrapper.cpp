// quote from cppreference.com:
// std::reference_wrapper is a class template that wraps a reference
// in a copyable, assignable object. It is frequently used as a mechanism
// to store references inside standard containers (like std::vector)
// which cannot normally hold references.

#include <cybozu/test.hpp>

#include <functional>
#include <iostream>
#include <vector>

struct foo {
    int i;
};

AUTOTEST(intref) {
    int *p = nullptr;
    std::reference_wrapper<int> a[3] = {*p, *p, *p};
    int i = 3;
    a[0] = std::ref(i);
    cybozu_assert( &(a[2].get()) == nullptr );
    a[0].get() = 5;
    cybozu_assert( i == 5 );
}

AUTOTEST(structref) {
    foo f;
    f.i = 3;

    std::vector<std::reference_wrapper<const foo>> v;
    v.emplace_back(f);
    cybozu_assert( v[0].get().i == 3 );
    f.i = 5;
    cybozu_assert( v[0].get().i == 5 );
}
