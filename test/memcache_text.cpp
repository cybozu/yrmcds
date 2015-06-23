#include "../src/memcache/memcache.hpp"

#include <cybozu/test.hpp>

#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <iostream>
#include <limits>
#include <stdlib.h>

using namespace yrmcds;
using namespace yrmcds::memcache;

const char CR = '\x0d';
const char NL = '\x0a';
const char SP = '\x20';

template<typename UInt>
inline UInt to_uint(const char* p, bool& result) {
    result = false;
    char* end;
    unsigned long long i = strtoull(p, &end, 10);
    if( i == 0 && p == end ) return 0;
    if( i == std::numeric_limits<unsigned long long>::max() &&
        errno == ERANGE ) return 0;
    char c = *end;
    if( c != CR && c != NL && c != SP ) return 0;
    if( i > std::numeric_limits<UInt>::max() ) return 0;
    result = true;
    return static_cast<UInt>(i);
}

AUTOTEST(to_uint) {
    bool r;
    to_uint<std::uint32_t>("\n", r);
    cybozu_assert( ! r );
    to_uint<std::uint32_t>("4294967295", r);
    cybozu_assert( ! r );
    to_uint<std::uint32_t>("4294967295 ", r);
    cybozu_assert( r );
    to_uint<std::uint32_t>("4294967295\r", r);
    cybozu_assert( r );
    to_uint<std::uint32_t>("4294967295\n", r);
    cybozu_assert( r );
    to_uint<std::uint32_t>("4294967296\n", r);
    cybozu_assert( ! r );
    to_uint<std::uint64_t>("4294967296\n", r);
    cybozu_assert( r );
    cybozu_assert( to_uint<std::uint64_t>("4294967296\n", r) == 4294967296ULL );
    to_uint<std::uint32_t>("-1\n", r);
    cybozu_assert( ! r );
    to_uint<std::uint32_t>(" 429496729\n", r);
    cybozu_assert( r );
    to_uint<std::uint32_t>(" 429496729\n", r);
    cybozu_assert( r );
    to_uint<std::uint32_t>(" 429496729\n", r);
    cybozu_assert( r );
    cybozu_assert( to_uint<std::uint64_t>("18446744073709551615 ", r) ==
                   18446744073709551615ULL );
    to_uint<std::uint64_t>("18446744073709551616 ", r);
    cybozu_assert( ! r );
}

#define MEMCACHE_TEST(n, d) \
    char data##n[] = d; \
    text_request t##n(data##n, sizeof(data##n) - 1);

#define VERIFY(n, k, f) \
    cybozu_assert( std::memcmp(std::get<0>(t##n.f()), k, sizeof(k)-1) == 0 )

AUTOTEST(empty) {
    MEMCACHE_TEST(1, "\r\n");
    cybozu_assert( t1.length() == 2 );
    cybozu_assert( ! t1.valid() );

    MEMCACHE_TEST(2, " \n");
    cybozu_assert( t2.length() == 2 );
    cybozu_assert( ! t2.valid() );
}

AUTOTEST(verbosity) {
    MEMCACHE_TEST(1, " verbosity");
    cybozu_assert( t1.length() == 0 );

    MEMCACHE_TEST(2, " verbosity\naaa");
    cybozu_assert( t2.length() == 11 );
    cybozu_assert( ! t2.valid() );

    MEMCACHE_TEST(3, " verbosity  \r\n");
    cybozu_assert( t3.command() == text_command::VERBOSITY );
    cybozu_assert( t3.length() != 0 );
    cybozu_assert( ! t3.valid() );
    cybozu_assert( ! t3.no_reply() );

    MEMCACHE_TEST(4, " verbosity  warning  \r\naaa");
    cybozu_assert( t4.valid() );
    cybozu_assert( t4.verbosity() == cybozu::severity::warning );
    cybozu_assert( ! t4.no_reply() );

    MEMCACHE_TEST(5, " verbosity  warning  \r\nverbosity error\r\n");
    cybozu_assert( t5.valid() );
    cybozu_assert( t5.verbosity() == cybozu::severity::warning );
    text_request t5_2(&data5[t5.length()], sizeof(data5) - 1 - t5.length());
    cybozu_assert( t5_2.valid() );
    cybozu_assert( t5_2.verbosity() == cybozu::severity::error );

    MEMCACHE_TEST(6, " verbosity  1\r\n");
    cybozu_assert( t6.valid() );

    MEMCACHE_TEST(7, " verbosity  error  noreply\r\n");
    cybozu_assert( t7.valid() );
    cybozu_assert( t7.no_reply() );
}

AUTOTEST(set) {
    MEMCACHE_TEST(1, "set \r\n ");
    cybozu_assert( t1.length() == 6 );
    cybozu_assert( t1.command() == text_command::SET );

    MEMCACHE_TEST(2, "set abcdef \r\n");
    cybozu_assert( std::get<1>(t2.key()) == 6 );
    VERIFY(2, "abcdef", key);
    cybozu_assert( ! t2.valid() );

    MEMCACHE_TEST(3, "set abcdef  65536 \r\n");
    cybozu_assert( t3.flags() == 65536 );

    MEMCACHE_TEST(4, "set abcdef  0 \r\n");
    cybozu_assert( t4.flags() == 0 );

    MEMCACHE_TEST(5, "set abcdef  4294967295 \r\n");
    cybozu_assert( t5.flags() == 4294967295ULL );

    MEMCACHE_TEST(6, "set abcdef  4294967296 \r\n");
    cybozu_assert( t6.flags() != 4294967296ULL );

    MEMCACHE_TEST(7, "set   abcdef   3 0 \r\n");
    cybozu_assert( t7.flags() == 3 );
    cybozu_assert( t7.exptime() == 0 );
    cybozu_assert( ! t7.valid() );

    MEMCACHE_TEST(8, "set   abcdef   3 100 \r\n");
    cybozu_assert( t8.exptime() < std::time(nullptr) + 101 );
    cybozu_assert( t8.exptime() > 100 );

    MEMCACHE_TEST(9, "set   abcdef   3 3000000 \r\n");
    cybozu_assert( t9.exptime() == 3000000 );

    MEMCACHE_TEST(10, "set aaa 100 0 5\r\nabcde\r\nset aaa");
    cybozu_assert( t10.valid() );
    cybozu_assert( std::get<1>(t10.data()) == 5 );
    VERIFY(10, "abcde", data);

    MEMCACHE_TEST(11, "set aaa 100 0 10\r\nabcdefghija\r\nset aaa");
    cybozu_assert( ! t11.valid() );

    MEMCACHE_TEST(12, "set aaa 100 0 10\r\nabcdefghij\r");
    cybozu_assert( t12.length() == 0 );

    MEMCACHE_TEST(13, "set aaa 100 0 10\r\nabcdefghij");
    cybozu_assert( t13.length() == 0 );

    MEMCACHE_TEST(14, "set aaa 100 0 10 \r\nabcdefghij\r\naaa");
    cybozu_assert( t14.valid() );
    cybozu_assert( ! t14.no_reply() );

    MEMCACHE_TEST(15, "set aaa 100 0 10 noreply   \r\nabcdefghij\r\naaa");
    cybozu_assert( t15.valid() );
    cybozu_assert( t15.no_reply() );

    MEMCACHE_TEST(16, "set aaa 100 0 10 norepry\r\nabcdefghij\r\naaa");
    cybozu_assert( ! t16.valid() );

    MEMCACHE_TEST(17, "set aaa 100 0 10 noreply aaa \r\nabcdefghij\r\naaa");
    cybozu_assert( ! t17.valid() );
}

AUTOTEST(add) {
    MEMCACHE_TEST(1, " add aaa 100 0 10 noreply\r\nabcdefghij\r\n");
    cybozu_assert( t1.valid() );
    cybozu_assert( t1.command() == text_command::ADD );
    cybozu_assert( t1.no_reply() );
}

AUTOTEST(replace) {
    MEMCACHE_TEST(1, " replace aaa 100 0 10 noreply   \r\nabcdefghij\r\n");
    cybozu_assert( t1.valid() );
    cybozu_assert( t1.command() == text_command::REPLACE );
    cybozu_assert( t1.no_reply() );
}

AUTOTEST(append) {
    MEMCACHE_TEST(1, " append aaa 100 0 10 noreply   \r\nabcdefghij\r\n");
    cybozu_assert( t1.valid() );
    cybozu_assert( t1.command() == text_command::APPEND );
    cybozu_assert( t1.no_reply() );
}

AUTOTEST(prepend) {
    MEMCACHE_TEST(1, " prepend aaa 100 0 10 noreply   \r\nabcdefghij\r\n");
    cybozu_assert( t1.valid() );
    cybozu_assert( t1.command() == text_command::PREPEND );
    cybozu_assert( t1.no_reply() );
}

AUTOTEST(cas) {
    MEMCACHE_TEST(1, " cas aaa 100 0 10 noreply   \r\nabcdefghij\r\n");
    cybozu_assert( ! t1.valid() );
    cybozu_assert( t1.command() == text_command::CAS );

    MEMCACHE_TEST(2, " cas aaa 100 0 10 3 noreply   \r\nabcdefghij\r\n");
    cybozu_assert( t2.valid() );
    cybozu_assert( t2.cas_unique() == 3 );
    cybozu_assert( t2.no_reply() );

    MEMCACHE_TEST(3, " cas aaa 100 0 10 4\r\nabcdefghij\r\n");
    cybozu_assert( t3.valid() );
    cybozu_assert( t3.cas_unique() == 4 );
    cybozu_assert( ! t3.no_reply() );
}

AUTOTEST(delete) {
    MEMCACHE_TEST(1, "delete  aaa   noreply   \r\nabc");
    cybozu_assert( t1.valid() );
    cybozu_assert( t1.command() == text_command::DELETE );
    cybozu_assert( std::memcmp(std::get<0>(t1.key()), "aaa", 3) == 0 );
    cybozu_assert( std::get<1>(t1.key()) == 3 );
    cybozu_assert( t1.no_reply() );

    MEMCACHE_TEST(2, "delete  aaa garbage\r\nabc");
    cybozu_assert( ! t2.valid() );

    MEMCACHE_TEST(3, "delete  aaagarbage\r\nabc");
    cybozu_assert( t3.valid() );
    cybozu_assert( std::get<1>(t3.key()) == 10 );
    cybozu_assert( ! t3.no_reply() );
}

AUTOTEST(touch) {
    MEMCACHE_TEST(1, "touch\r\nabc");
    cybozu_assert( ! t1.valid() );
    cybozu_assert( t1.command() == text_command::TOUCH );

    MEMCACHE_TEST(2, "touch abcd \r\nabc");
    cybozu_assert( ! t2.valid() );
    cybozu_assert( std::get<1>(t2.key()) == 4 );

    MEMCACHE_TEST(3, "touch abcd qqq\r\nabc");
    cybozu_assert( ! t3.valid() );

    MEMCACHE_TEST(4, "touch abcd 0 \r\nabc");
    cybozu_assert( t4.valid() );
    cybozu_assert( ! t4.no_reply() );
    cybozu_assert( t4.exptime() == 0 );

    MEMCACHE_TEST(5, "touch abcd 100 noreply\r\nabc");
    cybozu_assert( t5.valid() );
    cybozu_assert( t5.no_reply() );

    MEMCACHE_TEST(6, "touch abcd 100 noreply   38\r\nabc");
    cybozu_assert( ! t6.valid() );
    cybozu_assert( t6.length() == 29 );
}

AUTOTEST(stats) {
    MEMCACHE_TEST(1, "stats\r\nabc");
    cybozu_assert( t1.valid() );
    cybozu_assert( t1.command() == text_command::STATS );
    cybozu_assert( t1.stats() == stats_t::GENERAL );

    MEMCACHE_TEST(2, "stats hoge \r\nabc");
    cybozu_assert( ! t2.valid() );

    MEMCACHE_TEST(3, "stats items \r\nabc");
    cybozu_assert( t3.valid() );
    cybozu_assert( t3.stats() == stats_t::ITEMS );

    MEMCACHE_TEST(4, "stats items allow garbage\r\nabc");
    cybozu_assert( t4.valid() );
    cybozu_assert( t4.stats() == stats_t::ITEMS );

    MEMCACHE_TEST(5, "stats settings\r\nabc");
    cybozu_assert( t5.valid() );
    cybozu_assert( t5.stats() == stats_t::SETTINGS );

    MEMCACHE_TEST(6, "stats  sizes\r\nabc");
    cybozu_assert( t6.valid() );
    cybozu_assert( t6.stats() == stats_t::SIZES );
}

AUTOTEST(incr) {
    MEMCACHE_TEST(1, "incr\r\nabc");
    cybozu_assert( ! t1.valid() );
    cybozu_assert( t1.command() == text_command::INCR );

    MEMCACHE_TEST(2, "incr aaaaaa \r\nabc");
    cybozu_assert( ! t2.valid() );
    cybozu_assert( std::get<1>(t2.key()) == 6 );

    MEMCACHE_TEST(3, "incr a 429496729500 \r\nabc");
    cybozu_assert( t3.valid() );
    cybozu_assert( t3.value() == 429496729500ULL );
    cybozu_assert( ! t3.no_reply() );

    MEMCACHE_TEST(4, "incr a noreply \r\nabc");
    cybozu_assert( ! t4.valid() );

    MEMCACHE_TEST(5, "incr a 3 garbagenowallowed\r\nabc");
    cybozu_assert( ! t5.valid() );

    MEMCACHE_TEST(6, "incr a 3 noreply garbagenowallowed\r\nabc");
    cybozu_assert( ! t6.valid() );

    MEMCACHE_TEST(7, "incr a 3 noreply \r\nabc");
    cybozu_assert( t7.valid() );
    cybozu_assert( t7.no_reply() );
}

AUTOTEST(decr) {
    MEMCACHE_TEST(1, "decr   tttt   100  noreply  \r\nabc");
    cybozu_assert( t1.valid() );
    cybozu_assert( t1.command() == text_command::DECR );
    cybozu_assert( std::get<1>(t1.key()) == 4 );
    cybozu_assert( t1.value() == 100 );
    cybozu_assert( t1.no_reply() );
}

AUTOTEST(lock) {
    MEMCACHE_TEST(1, "lock hoge \r\n");
    cybozu_assert( t1.valid() );
    cybozu_assert( t1.command() == text_command::LOCK );
    cybozu_assert( std::get<1>(t1.key()) == 4 );
    VERIFY(1, "hoge", key);

    MEMCACHE_TEST(2, "lock hoge leftover\r\n");
    cybozu_assert( ! t2.valid() );
    cybozu_assert( t2.length() == 20 );
}

AUTOTEST(unlock) {
    MEMCACHE_TEST(1, "unlock hoge \r\n");
    cybozu_assert( t1.valid() );
    cybozu_assert( t1.command() == text_command::UNLOCK );
    cybozu_assert( std::get<1>(t1.key()) == 4 );
    VERIFY(1, "hoge", key);

    MEMCACHE_TEST(2, "unlock hoge leftover\r\n");
    cybozu_assert( ! t2.valid() );
    cybozu_assert( t2.length() == 22 );
}

AUTOTEST(unlockall) {
    MEMCACHE_TEST(1, "unlock_all \r\n");
    cybozu_assert( t1.valid() );
    cybozu_assert( t1.command() == text_command::UNLOCK_ALL );
}

AUTOTEST(get) {
    MEMCACHE_TEST(1, "get  \r\nabc");
    cybozu_assert( ! t1.valid() );
    cybozu_assert( t1.length() == 7 );

    MEMCACHE_TEST(2, "get 38338 \r\nabc");
    cybozu_assert( t2.valid() );
    cybozu_assert( t2.command() == text_command::GET );
    cybozu_assert( std::get<1>(t2.first_key()) == 5 );
    VERIFY(2, "38338", first_key);
    cybozu_assert( t2.next_key(t2.first_key()) == text_request::eos );

    MEMCACHE_TEST(3, "get 38338 4\r\nabc");
    cybozu_assert( t3.valid() );
    cybozu_assert( std::get<1>(t3.first_key()) == 5 );
    VERIFY(3, "38338", first_key);
    auto next_key = t3.next_key(t3.first_key());
    cybozu_assert( next_key != text_request::eos );
    cybozu_assert( std::get<1>(next_key) == 1 );
    cybozu_assert( *std::get<0>(next_key) == '4' );
    next_key = t3.next_key(next_key);
    cybozu_assert( next_key == text_request::eos );

    MEMCACHE_TEST(4, "get 38338 4  \r\nabc");
    cybozu_assert( t4.valid() );
    next_key = t4.next_key(t4.first_key());
    cybozu_assert( std::get<1>(next_key) == 1 );
    cybozu_assert( *std::get<0>(next_key) == '4' );
    next_key = t4.next_key(next_key);
    cybozu_assert( next_key == text_request::eos );
}

AUTOTEST(gets) {
    MEMCACHE_TEST(1, "gets 38338 \r\nabc");
    cybozu_assert( t1.valid() );
    cybozu_assert( t1.command() == text_command::GETS );
}

AUTOTEST(flush_all) {
    MEMCACHE_TEST(1, "flush_all \r\nabc");
    cybozu_assert( t1.valid() );
    cybozu_assert( t1.command() == text_command::FLUSH_ALL );

    MEMCACHE_TEST(2, "flush_all 100 \r\nabc");
    cybozu_assert( t2.valid() );
    cybozu_assert( ! t2.no_reply() );
    cybozu_assert( t2.exptime() != 0 );
    cybozu_assert( t2.exptime() < (time(NULL) + 101) );

    MEMCACHE_TEST(3, "flush_all 100a \r\nabc");
    cybozu_assert( ! t3.valid() );

    MEMCACHE_TEST(4, "flush_all 100 noreply \r\nabc");
    cybozu_assert( t4.valid() );
    cybozu_assert( t4.no_reply() );
}

AUTOTEST(keys) {
    MEMCACHE_TEST(1, "keys\r\n");
    cybozu_assert( t1.valid() );
    cybozu_assert( t1.command() == text_command::KEYS );
    cybozu_assert( std::get<1>(t1.key()) == 0 );

    MEMCACHE_TEST(2, "keys abc \r\n");
    cybozu_assert( t2.valid() );
    cybozu_assert( t2.command() == text_command::KEYS );
    cybozu_assert( std::get<1>(t2.key()) == 3 );
    VERIFY(2, "abc", key);
}

AUTOTEST(singles) {
    MEMCACHE_TEST(1, "slabs 38338 \r\nabc");
    cybozu_assert( t1.valid() );
    cybozu_assert( t1.command() == text_command::SLABS );

    MEMCACHE_TEST(2, "version 38338 \r\nabc");
    cybozu_assert( t2.valid() );
    cybozu_assert( t2.command() == text_command::VERSION );

    MEMCACHE_TEST(3, "quit 38338 \r\nabc");
    cybozu_assert( t3.valid() );
    cybozu_assert( t3.command() == text_command::QUIT );
}
