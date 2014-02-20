#include "../src/config.hpp"
#include "../src/memcache/object.hpp"

#include <cybozu/test.hpp>

#include <cstdlib>

using yrmcds::memcache::object;
using cybozu::dynbuf;

std::size_t reset_heap_limit() {
    const char* p = std::getenv("HEAP_LIMIT");
    std::size_t n = 0;
    if( p != nullptr )
        n = (std::size_t)std::atoi(p);
    if( n == 0 )
        n = yrmcds::DEFAULT_HEAP_DATA_LIMIT;

    yrmcds::g_config.set_heap_data_limit(n);
    return n;
}

AUTOTEST(set) {
    reset_heap_limit();
    object o1("abcde", 5, 100, 0);
    cybozu_assert( o1.size() == 5 );
    cybozu_assert( o1.flags() == 100 );
    dynbuf d1(0); d1.append("abcde", 5);
    dynbuf d1_(0);
    std::uint64_t cas = o1.cas_unique();
    cybozu_assert( o1.data(d1_) == d1 );
    cybozu_assert( o1.cas_unique() == cas );

    o1.set("123", 3, 200, 200);
    cybozu_assert( o1.size() == 3 );
    cybozu_assert( o1.flags() == 200 );
    d1.reset(); d1.append("123", 3);
    cybozu_assert( o1.data(d1_) == d1 );
    cybozu_assert( o1.cas_unique() != cas );
}

AUTOTEST(append_prepend) {
    reset_heap_limit();
    object o1("abcde", 5, 100, 0);
    std::uint64_t cas = o1.cas_unique();
    o1.append("12345", 5);
    std::uint64_t cas2 = o1.cas_unique();
    cybozu_assert( o1.size() == 10 );
    cybozu_assert( cas != cas2 );
    dynbuf d1(0); d1.append("abcde12345", 10);
    dynbuf d1_(0);
    cybozu_assert( o1.data(d1_) == d1 );

    o1.prepend("#*A", 3);
    std::uint64_t cas3 = o1.cas_unique();
    cybozu_assert( o1.size() == 13 );
    cybozu_assert( cas != cas3 );
    cybozu_assert( cas2 != cas3 );
    d1.reset(); d1.append("#*Aabcde12345", 13);
    cybozu_assert( o1.data(d1_) == d1 );
}

AUTOTEST(touch) {
    reset_heap_limit();
    object o1("abcde", 5, 100, 0);
    std::uint64_t cas = o1.cas_unique();
    o1.touch(1111);
    dynbuf d1(0); d1.append("abcde", 5);
    dynbuf d1_(0);
    cybozu_assert( o1.size() == 5 );
    cybozu_assert( o1.data(d1_) == d1 );
    cybozu_assert( o1.cas_unique() == cas );
}

AUTOTEST(incr_decr) {
    if( reset_heap_limit() < 24 ) return;
    object o1("abcde", 5, 100, 0);
    cybozu_test_exception( o1.incr(3), object::not_a_number );
    object o2("123ab", 5, 100, 0);
    std::uint64_t cas = o2.cas_unique();
    std::uint64_t n = 0;
    cybozu_test_no_exception( n = o2.incr(3) );
    std::uint64_t cas2 = o2.cas_unique();
    cybozu_assert( n == 126 );
    cybozu_assert( cas != cas2 );
    n = o2.decr(3);
    std::uint64_t cas3 = o2.cas_unique();
    cybozu_assert( n == 123 );
    cybozu_assert( cas2 != cas3 );
    n = o2.decr(200);
    cybozu_assert( n == 0 );
    object o3("  18446744073709551615", 22, 100, 0);
    n = o3.decr(1);
    cybozu_assert( n == 18446744073709551614ULL );
    n = o3.incr(2);
    cybozu_assert( n == 0 );
    o3.set(" 111", 4, 0, 0);
    dynbuf d3(0); d3.append(" 111", 4);
    dynbuf d3_(0);
    cybozu_assert( o3.data(d3_) == d3 );
}
