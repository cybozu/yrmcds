#include "../src/semaphore/semaphore.hpp"

#include <cybozu/test.hpp>

#include <cstring>

using namespace yrmcds::semaphore;

#define REQ(n, data) \
    const char i##n[] = data; \
    request r##n(i##n, sizeof(i##n) - 1);

bool operator==(const string_slice& lhs, const char* rhs) {
    if (lhs.len != std::strlen(rhs))
        return false;
    return std::memcmp(lhs.p, rhs, lhs.len) == 0;
}

AUTOTEST(noop) {
    // incomplete request
    REQ(1, "\x90\x00\x00\x00");
    cybozu_assert( r1.length() == 0 );

    // valid request
    REQ(2, "\x90\x00\x00\x00"
           "\x00\x00\x00\x00"
           "\x01\x02\x03\x04");
    cybozu_assert( r2.length() == 12 );
    cybozu_assert( r2.status() == status::OK );
    cybozu_assert( r2.command() == command::Noop );
    cybozu_assert( std::memcmp(r2.opaque(), "\01\02\03\04", 4) == 0 );

    // valid request + incomplete request
    REQ(3, "\x90\x00\x00\x00"
           "\x00\x00\x00\x00"
           "\x01\x02\x03\x04"
           "\x90\x01\x00\x00");
    cybozu_assert( r3.length() == 12 );
    cybozu_assert( r3.status() == status::OK );
    cybozu_assert( r3.command() == command::Noop );
    cybozu_assert( std::memcmp(r3.opaque(), "\01\02\03\04", 4) == 0 );
}

AUTOTEST(get) {
    // valid request
    REQ(1, "\x90\x01\x00\x00"  // magic, opcode, flags, reserved
           "\x00\x00\x00\x07"  // body length
           "\x01\x02\x03\x04"  // opaque
           "\x00\x05"          // name length
           "Hello")            // name
    cybozu_assert( r1.length() == 12 + 7 );
    cybozu_assert( r1.status() == status::OK );
    cybozu_assert( r1.command() == command::Get );
    cybozu_assert( r1.name() == "Hello" );

    // invalid request (name length == 0)
    REQ(2, "\x90\x01\x00\x00"  // magic, opcode, flags, reserved
           "\x00\x00\x00\x02"  // body length
           "\x01\x02\x03\x04"  // opaque
           "\x00\x00")         // name length
    cybozu_assert( r2.length() == 12 + 2 );
    cybozu_assert( r2.status() == status::Invalid );
    cybozu_assert( r2.command() == command::Get );

    // invalid request (body length < 2 + name length)
    REQ(3, "\x90\x01\x00\x00"  // magic, opcode, flags, reserved
           "\x00\x00\x00\x06"  // body length
           "\x01\x02\x03\x04"  // opaque
           "\x00\x05"          // name length
           "Hello")            // name
    cybozu_assert( r3.length() == 12 + 6 );
    cybozu_assert( r3.status() == status::Invalid );
    cybozu_assert( r3.command() == command::Get );

    // valid request (name includes 0x00)
    REQ(4, "\x90\x01\x00\x00"  // magic, opcode, flags, reserved
           "\x00\x00\x00\x07"  // body length
           "\x01\x02\x03\x04"  // opaque
           "\x00\x05"          // name length
           "He\x00lo")         // name
    cybozu_assert( r4.length() == 12 + 7 );
    cybozu_assert( r4.status() == status::OK );
    cybozu_assert( r4.command() == command::Get );
    cybozu_assert( r4.name().len == 5 );
    cybozu_assert( std::memcmp(r4.name().p, "He\x00lo", 5) == 0 );
}

AUTOTEST(acquire) {
    // valid request
    REQ(1, "\x90\x02\x00\x00"  // magic, opcode, flags, reserved
           "\x00\x00\x00\x0b"  // body length
           "\x01\x02\x03\x04"  // opaque
           "\x00\x00\x00\x02"  // resources
           "\x00\x00\x00\x0a"  // initial resources
           "\x00\x01"          // name length
           "x")                // name
    cybozu_assert( r1.length() == 12 + 11 );
    cybozu_assert( r1.status() == status::OK );
    cybozu_assert( r1.command() == command::Acquire );
    cybozu_assert( r1.resources() == 2 );
    cybozu_assert( r1.initial() == 10 );
    cybozu_assert( r1.name() == "x" );

    // valid request (initial - resources == 0)
    REQ(2, "\x90\x02\x00\x00"  // magic, opcode, flags, reserved
           "\x00\x00\x00\x0b"  // body length
           "\x01\x02\x03\x04"  // opaque
           "\x00\x00\x00\x0a"  // resources
           "\x00\x00\x00\x0a"  // initial resources
           "\x00\x01"          // name length
           "x")                // name
    cybozu_assert( r2.length() == 12 + 11 );
    cybozu_assert( r2.status() == status::OK );
    cybozu_assert( r2.command() == command::Acquire );
    cybozu_assert( r2.resources() == 10 );
    cybozu_assert( r2.initial() == 10 );
    cybozu_assert( r2.name() == "x" );

    // invalid request (resources == 0)
    REQ(3, "\x90\x02\x00\x00"  // magic, opcode, flags, reserved
           "\x00\x00\x00\x0b"  // body length
           "\x01\x02\x03\x04"  // opaque
           "\x00\x00\x00\x00"  // resources
           "\x00\x00\x00\x0a"  // initial resources
           "\x00\x01"          // name length
           "x")                // name
    cybozu_assert( r3.length() == 12 + 11 );
    cybozu_assert( r3.status() == status::Invalid );
    cybozu_assert( r3.command() == command::Acquire );

    // invalid request (initial - resources < 0)
    REQ(4, "\x90\x02\x00\x00"  // magic, opcode, flags, reserved
           "\x00\x00\x00\x0b"  // body length
           "\x01\x02\x03\x04"  // opaque
           "\x00\x00\x00\x02"  // resources
           "\x00\x00\x00\x01"  // initial resources
           "\x00\x01"          // name length
           "x")                // name
    cybozu_assert( r4.length() == 12 + 11 );
    cybozu_assert( r4.status() == status::Invalid );
    cybozu_assert( r4.command() == command::Acquire );

    // invalid request (name length == 0)
    REQ(5, "\x90\x02\x00\x00"  // magic, opcode, flags, reserved
           "\x00\x00\x00\x0a"  // body length
           "\x01\x02\x03\x04"  // opaque
           "\x00\x00\x00\x02"  // resources
           "\x00\x00\x00\x0a"  // initial resources
           "\x00\x00")         // name length
    cybozu_assert( r5.length() == 12 + 10 );
    cybozu_assert( r5.status() == status::Invalid );
    cybozu_assert( r5.command() == command::Acquire );

    // invalid request (body length < 10 + name length)
    REQ(6, "\x90\x02\x00\x00"  // magic, opcode, flags, reserved
           "\x00\x00\x00\x0b"  // body length
           "\x01\x02\x03\x04"  // opaque
           "\x00\x00\x00\x02"  // resources
           "\x00\x00\x00\x0a"  // initial resources
           "\x00\x02"          // name length
           "xy")               // name
    cybozu_assert( r6.length() == 12 + 11 );
    cybozu_assert( r6.status() == status::Invalid );
    cybozu_assert( r6.command() == command::Acquire );
}

AUTOTEST(release) {
    // valid request
    REQ(1, "\x90\x03\x00\x00"  // magic, opcode, flags, reserved
           "\x00\x00\x00\x07"  // body length
           "\x01\x02\x03\x04"  // opaque
           "\x00\x00\x00\x02"  // resources
           "\x00\x01"          // name length
           "x")                // name
    cybozu_assert( r1.length() == 12 + 7 );
    cybozu_assert( r1.status() == status::OK );
    cybozu_assert( r1.command() == command::Release );
    cybozu_assert( r1.resources() == 2 );
    cybozu_assert( r1.name() == "x" );

    // valid request (resources == 0)
    REQ(2, "\x90\x03\x00\x00"  // magic, opcode, flags, reserved
           "\x00\x00\x00\x07"  // body length
           "\x01\x02\x03\x04"  // opaque
           "\x00\x00\x00\x00"  // resources
           "\x00\x01"          // name length
           "x")                // name
    cybozu_assert( r2.length() == 12 + 7 );
    cybozu_assert( r2.status() == status::OK );
    cybozu_assert( r2.command() == command::Release );
    cybozu_assert( r2.resources() == 0 );
    cybozu_assert( r2.name() == "x" );

    // invalid request (name length == 0)
    REQ(3, "\x90\x03\x00\x00"  // magic, opcode, flags, reserved
           "\x00\x00\x00\x06"  // body length
           "\x01\x02\x03\x04"  // opaque
           "\x00\x00\x00\x02"  // resources
           "\x00\x00")         // name length
    cybozu_assert( r3.length() == 12 + 6 );
    cybozu_assert( r3.status() == status::Invalid );
    cybozu_assert( r3.command() == command::Release );

    // invalid request (body length < 6 + name length)
    REQ(4, "\x90\x03\x00\x00"  // magic, opcode, flags, reserved
           "\x00\x00\x00\x06"  // body length
           "\x01\x02\x03\x04"  // opaque
           "\x00\x00\x00\x02"  // resources
           "\x00\x01"          // name length
           "x")                // name
    cybozu_assert( r4.length() == 12 + 6 );
    cybozu_assert( r4.status() == status::Invalid );
    cybozu_assert( r4.command() == command::Release );
}

AUTOTEST(stats) {
    REQ(1, "\x90\x10\x00\x00"
           "\x00\x00\x00\x00"
           "\x01\x02\x03\x04");
    cybozu_assert( r1.length() == 12 );
    cybozu_assert( r1.status() == status::OK );
    cybozu_assert( r1.command() == command::Stats );
}

AUTOTEST(dump) {
    REQ(1, "\x90\x11\x00\x00"
           "\x00\x00\x00\x00"
           "\x01\x02\x03\x04");
    cybozu_assert( r1.length() == 12 );
    cybozu_assert( r1.status() == status::OK );
    cybozu_assert( r1.command() == command::Dump );
}
