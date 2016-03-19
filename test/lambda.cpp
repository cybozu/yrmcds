#include <cybozu/test.hpp>

AUTOTEST(lambda) {
    int x = 3;
    auto foo = [=]() mutable { ++x; return x * x; };
    cybozu_assert( foo() == 16 );
    cybozu_assert( foo() == 25 );
}
