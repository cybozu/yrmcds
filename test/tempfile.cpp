#include "../src/tempfile.hpp"

#include <cybozu/dynbuf.hpp>
#include <cybozu/test.hpp>

#include <cstring>

AUTOTEST(mkstemp_wrap) {
    yrmcds::mkstemp_wrap("/var/tmp");
}

AUTOTEST(tempfile) {
    yrmcds::tempfile t;
    t.write("abcde", 5);
    cybozu::dynbuf buf(10);

    t.read_contents(buf);
    cybozu_assert( buf.size() == 5 );
    cybozu_assert( std::memcmp(buf.data(), "abcde", 5) == 0 );
    buf.reset();

    t.read_contents(buf);
    cybozu_assert( buf.size() == 5 );
    cybozu_assert( std::memcmp(buf.data(), "abcde", 5) == 0 );
    buf.reset();

    t.write("xxx", 3);
    t.read_contents(buf);
    cybozu_assert( buf.size() == 8 );
    cybozu_assert( std::memcmp(buf.data(), "abcdexxx", 8) == 0 );
    buf.reset();

    t.clear();
    t.read_contents(buf);
    cybozu_assert( buf.size() == 0 );

    buf.append("123", 3);
    t.write("789", 3);
    t.read_contents(buf);
    cybozu_assert( buf.size() == 6 );
    cybozu_assert( std::memcmp(buf.data(), "123789", 6) == 0 );
    buf.reset();
}
