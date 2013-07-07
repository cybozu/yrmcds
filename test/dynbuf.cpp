#include <cybozu/dynbuf.hpp>
#include <cybozu/test.hpp>

#include <cstring>

AUTOTEST(dynbuf0) {
    cybozu::dynbuf b(0);
    cybozu_assert( b.empty() );
    b.append("abc", 3);
    cybozu_assert( b.size() == 3 );
    cybozu_assert( ! b.empty() );
    b.append("def", 4);
    cybozu_assert( b.size() == 7 );
    cybozu_assert( std::memcmp(b.data(), "abcdef", 7) == 0 );
    b.reset();
    cybozu_assert( b.size() == 0 );
    char* p = b.prepare(7);
    std::memcpy(p, "aaa", 4);
    b.consume(4);
    cybozu_assert( b.size() == 4 );
    cybozu_assert( std::memcmp(b.data(), "aaa", 4) == 0 );
    b.append("1111112345678", 13);
    b.erase(10);
    cybozu_assert( b.size() == 7 );
    cybozu_assert( std::memcmp(b.data(), "2345678", 7) == 0 );
}

AUTOTEST(dynbuf5) {
    cybozu::dynbuf b(0);
    cybozu_assert( b.empty() );
    b.append("abc", 3);
    b.append("defghi", 6);
    cybozu_assert( b.size() == 9 );
    cybozu_assert( std::memcmp(b.data(), "abcdefghi", 9) == 0 );
    b.erase(6);
    cybozu_assert( b.size() == 3 );
    cybozu_assert( std::memcmp(b.data(), "ghi", 3) == 0 );
    b.append("0123456789", 10);
    b.erase(1);
    cybozu_assert( b.size() == 12 );
    cybozu_assert( std::memcmp(b.data(), "hi0123456789", 12) == 0 );
    b.reset();
}
