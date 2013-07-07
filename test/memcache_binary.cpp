#include "../src/memcache.hpp"

#include <cybozu/test.hpp>

#include <cstring>
#include <tuple>

using namespace yrmcds::memcache;

#define REQ(n, data) \
    const char i##n[] = data; \
    binary_request r##n(i##n, sizeof(i##n) - 1);
#define ITEMCMP(t, s) \
    cybozu_assert( std::get<1>(t) == (sizeof(s) - 1) ); \
    cybozu_assert( std::memcmp(std::get<0>(t), s, sizeof(s) - 1) == 0 );

AUTOTEST(get) {
    REQ(1, "\x80\x00\x00\x00");
    cybozu_assert( r1.length() == 0 );
    REQ(2, "\x80\x00\x00\x05\x00\x00\x00\x00"
        "\x00\x00\x00\x05" // total body
        "\x12\x34\x56\x78" // opaque
        "\x00\x00\x00\x00\x00\x00\x00\x01" // CAS
        "Hello" // key
        );
    cybozu_assert( r2.length() == (24 + 5) );
    cybozu_assert( r2.status() == binary_status::OK );
    cybozu_assert( r2.command() == binary_command::Get );
    cybozu_assert( ! r2.quiet() );
    cybozu_assert( r2.key() == item(i2 + 24, 5) );
    cybozu_assert( r2.exptime() == binary_request::EXPTIME_NONE );
    ITEMCMP(r2.key(), "Hello");
    cybozu_assert( std::memcmp(r2.opaque(), "\x12\x34\x56\x78", 4) == 0 );
    cybozu_assert( r2.cas_unique() == 1 );
    REQ(3, "\x80\x00\x00\x05\x00\x00\x00\x00"
        "\x00\x00\x00\x05" // total body
        "\x12\x34\x56\x78" // opaque
        "\x00\x00\x00\x00\x00\x00\x00\x01" // CAS
        "Hell" // key
        );
    cybozu_assert( r3.length() == 0 );
    REQ(4, "\x80\x00\x00\x06\x00\x00\x00\x00"
        "\x00\x00\x00\x05" // total body
        "\x12\x34\x56\x78" // opaque
        "\x00\x00\x00\x00\x00\x00\x00\x01" // CAS
        "Hello " // key
        );
    cybozu_assert( r4.length() == (24 + 5) );
    cybozu_assert( r4.status() == binary_status::Invalid );
    REQ(5, "\x80\x00\x00\x05\x04\x00\x00\x00"
        "\x00\x00\x00\x09" // total body
        "\x12\x34\x56\x78" // opaque
        "\x00\x00\x00\x00\x00\x00\x00\x01" // CAS
        "\x00\x00\x01\x00" // extra (exptime)
        "Hello " // key
        );
    cybozu_assert( r5.length() == (24 + 9) );
    cybozu_assert( r5.status() == binary_status::OK );
    cybozu_assert( r5.exptime() != 0 &&
                   r5.exptime() != binary_request::EXPTIME_NONE );

    const char data6[] =
        "\x80\x00\x00\x05\x04\x00\x00\x00"
        "\x00\x00\x00\x09" // total body
        "\x12\x34\x56\x78" // opaque
        "\x00\x00\x00\x00\x00\x00\x00\x01" // CAS
        "\x00\x00\x01\x00" // extra (exptime)
        "Hello" // key
        "\x80\x09\x00\x05\x00\x00\x00\x00"
        "\x00\x00\x00\x05" // total body
        "\x12\x34\x56\x78" // opaque
        "\x00\x00\x00\x00\x00\x00\x00\x01" // CAS
        "Hello "; // key
    binary_request r6(data6, sizeof(data6) - 1);
    binary_request r6_2(data6 + r6.length(), sizeof(data6) - r6.length() - 1);
    cybozu_assert( r6_2.status() == binary_status::OK );
    cybozu_assert( r6_2.command() == binary_command::GetQ );
    cybozu_assert( r6_2.quiet() );

    REQ(7, "\x80\x81\x00\x00\x00\x00\x00\x00"
        "\x00\x00\x00\x00" // total body
        "\x12\x34\x56\x78" // opaque
        "\x00\x00\x00\x00\x00\x00\x00\x00" // CAS
        );
    cybozu_assert( r7.length() == 24 );
    cybozu_assert( r7.status() == binary_status::UnknownCommand );

    REQ(8, "\x80\x0c\x00\x05\x04\x00\x00\x00"
        "\x00\x00\x00\x09" // total body
        "\x12\x34\x56\x78" // opaque
        "\x00\x00\x00\x00\x00\x00\x00\x01" // CAS
        "\x00\x00\x01\x00" // extra (exptime)
        "Hello " // key
        );
    cybozu_assert( r8.length() != 0 );
    cybozu_assert( r8.status() == binary_status::OK );
    cybozu_assert( r8.command() == binary_command::GetK );
    cybozu_assert( ! r8.quiet() );

    REQ(9, "\x80\x0d\x00\x05\x04\x00\x00\x00"
        "\x00\x00\x00\x09" // total body
        "\x12\x34\x56\x78" // opaque
        "\x00\x00\x00\x00\x00\x00\x00\x01" // CAS
        "\x00\x00\x01\x00" // extra (exptime)
        "Hello " // key
        );
    cybozu_assert( r9.length() != 0 );
    cybozu_assert( r9.status() == binary_status::OK );
    cybozu_assert( r9.command() == binary_command::GetKQ );
    cybozu_assert( r9.quiet() );
    REQ(10, "\x80\x1d\x00\x05\x04\x00\x00\x00"
        "\x00\x00\x00\x09" // total body
        "\x12\x34\x56\x78" // opaque
        "\x00\x00\x00\x00\x00\x00\x00\x01" // CAS
        "\x00\x00\x01\x00" // extra (exptime)
        "Hello" // key
        );
    cybozu_assert( r10.length() != 0 );
    cybozu_assert( r10.status() == binary_status::OK );
    cybozu_assert( r10.command() == binary_command::GaT );
    cybozu_assert( ! r10.quiet() );
    cybozu_assert( r10.exptime() != binary_request::EXPTIME_NONE );
    REQ(11, "\x80\x1e\x00\x05\x04\x00\x00\x00"
        "\x00\x00\x00\x09" // total body
        "\x12\x34\x56\x78" // opaque
        "\x00\x00\x00\x00\x00\x00\x00\x01" // CAS
        "\x00\x00\x01\x00" // extra (exptime)
        "Hello" // key
        );
    cybozu_assert( r11.length() != 0 );
    cybozu_assert( r11.status() == binary_status::OK );
    cybozu_assert( r11.command() == binary_command::GaTQ );
    cybozu_assert( r11.quiet() );
    REQ(12, "\x80\x23\x00\x05\x04\x00\x00\x00"
        "\x00\x00\x00\x09" // total body
        "\x12\x34\x56\x78" // opaque
        "\x00\x00\x00\x00\x00\x00\x00\x01" // CAS
        "\x00\x00\x01\x00" // extra (exptime)
        "Hello" // key
        );
    cybozu_assert( r12.length() != 0 );
    cybozu_assert( r12.status() == binary_status::OK );
    cybozu_assert( r12.command() == binary_command::GaTK );
    cybozu_assert( ! r12.quiet() );
    REQ(13, "\x80\x24\x00\x05\x04\x00\x00\x00"
        "\x00\x00\x00\x09" // total body
        "\x12\x34\x56\x78" // opaque
        "\x00\x00\x00\x00\x00\x00\x00\x01" // CAS
        "\x00\x00\x01\x00" // extra (exptime)
        "Hello" // key
        );
    cybozu_assert( r13.length() != 0 );
    cybozu_assert( r13.status() == binary_status::OK );
    cybozu_assert( r13.command() == binary_command::GaTKQ );
    cybozu_assert( r13.quiet() );
}

AUTOTEST(set_add_replace) {
    REQ(1, "\x80\x01\x00\x05\x08\x00\x00\x00"
        "\x00\x00\x00\x12" // total body
        "\x12\x34\x56\x78" // opaque
        "\x00\x00\x00\x00\x00\x00\x00\x10" // CAS
        "\x00\x00\x00\x20" // flags
        "\x11\x11\x22\x33" // extra (exptime)
        "Hello" // key
        "World" // body
        );
    cybozu_assert( r1.length() == (24 + 18) );
    cybozu_assert( r1.status() == binary_status::OK );
    cybozu_assert( r1.command() == binary_command::Set );
    cybozu_assert( ! r1.quiet() );
    cybozu_assert( r1.cas_unique() == 0x10 );
    cybozu_assert( r1.flags() == 0x20 );
    cybozu_assert( r1.exptime() == 0x11112233UL );
    ITEMCMP(r1.key(), "Hello");
    ITEMCMP(r1.data(), "World");
    REQ(2, "\x80\x01\x00\x00\x08\x00\x00\x00"
        "\x00\x00\x00\x0d" // total body
        "\x12\x34\x56\x78" // opaque
        "\x00\x00\x00\x00\x00\x00\x00\x10" // CAS
        "\x00\x00\x00\x20" // flags
        "\x00\x00\x00\x00" // extra (exptime)
        "World" // body
        );
    cybozu_assert( r2.length() == (24 + 13) );
    cybozu_assert( r2.status() == binary_status::Invalid );
    cybozu_assert( r2.exptime() == 0 );
    REQ(3, "\x80\x02\x00\x06\x08\x00\x00\x00"
        "\x00\x00\x00\x13" // total body
        "\x12\x34\x56\x78" // opaque
        "\x00\x00\x00\x00\x00\x00\x00\x10" // CAS
        "\x00\x00\x00\x20" // flags
        "\x11\x11\x22\x33" // extra (exptime)
        "Hello " // key
        "World" // body
        "\x80 "
        );
    cybozu_assert( r3.length() == (24 + 19) );
    cybozu_assert( r3.status() == binary_status::OK );
    cybozu_assert( r3.command() == binary_command::Add );
    cybozu_assert( ! r3.quiet() );
    REQ(4, "\x80\x03\x00\x06\x08\x00\x00\x00"
        "\x00\x00\x00\x13" // total body
        "\x12\x34\x56\x78" // opaque
        "\x00\x00\x00\x00\x00\x00\x00\x10" // CAS
        "\x00\x00\x00\x20" // flags
        "\x11\x11\x22\x33" // extra (exptime)
        "Hello " // key
        "World" // body
        "\x80 "
        );
    cybozu_assert( r4.length() == (24 + 19) );
    cybozu_assert( r4.status() == binary_status::OK );
    cybozu_assert( r4.command() == binary_command::Replace );
    cybozu_assert( ! r4.quiet() );
    REQ(5, "\x80\x11\x00\x06\x08\x00\x00\x00"
        "\x00\x00\x00\x13" // total body
        "\x12\x34\x56\x78" // opaque
        "\x00\x00\x00\x00\x00\x00\x00\x10" // CAS
        "\x00\x00\x00\x20" // flags
        "\x11\x11\x22\x33" // extra (exptime)
        "Hello " // key
        "World" // body
        );
    cybozu_assert( r5.length() != 0 );
    cybozu_assert( r5.status() == binary_status::OK );
    cybozu_assert( r5.command() == binary_command::SetQ );
    cybozu_assert( r5.quiet() );
    REQ(6, "\x80\x12\x00\x06\x08\x00\x00\x00"
        "\x00\x00\x00\x13" // total body
        "\x12\x34\x56\x78" // opaque
        "\x00\x00\x00\x00\x00\x00\x00\x10" // CAS
        "\x00\x00\x00\x20" // flags
        "\x11\x11\x22\x33" // extra (exptime)
        "Hello " // key
        "World" // body
        );
    cybozu_assert( r6.length() != 0 );
    cybozu_assert( r6.status() == binary_status::OK );
    cybozu_assert( r6.command() == binary_command::AddQ );
    cybozu_assert( r6.quiet() );
    REQ(7, "\x80\x13\x00\x06\x08\x00\x00\x00"
        "\x00\x00\x00\x13" // total body
        "\x12\x34\x56\x78" // opaque
        "\x00\x00\x00\x00\x00\x00\x00\x10" // CAS
        "\x00\x00\x00\x20" // flags
        "\x11\x11\x22\x33" // extra (exptime)
        "Hello " // key
        "World" // body
        );
    cybozu_assert( r7.length() != 0 );
    cybozu_assert( r7.status() == binary_status::OK );
    cybozu_assert( r7.command() == binary_command::ReplaceQ );
    cybozu_assert( r7.quiet() );
    REQ(8, "\x80\x02\x00\x06\x04\x00\x00\x00"
        "\x00\x00\x00\x0f" // total body
        "\x12\x34\x56\x78" // opaque
        "\x00\x00\x00\x00\x00\x00\x00\x10" // CAS
        "\x00\x00\x00\x20" // flags
        "Hello " // key
        "World" // body
        "\x80 "
        );
    cybozu_assert( r8.length() == (24 + 15) );
    cybozu_assert( r8.status() == binary_status::Invalid );
    REQ(9, "\x80\x13\x00\x00\x08\x00\x00\x00"
        "\x00\x00\x00\x0d" // total body
        "\x12\x34\x56\x78" // opaque
        "\x00\x00\x00\x00\x00\x00\x00\x10" // CAS
        "\x00\x00\x00\x20" // flags
        "\x11\x11\x22\x33" // extra (exptime)
        "World" // body
        );
    cybozu_assert( r9.length() != 0 );
    cybozu_assert( r9.status() == binary_status::Invalid );
}

AUTOTEST(delete) {
    REQ(1, "\x80\x04\x00\x05\x00\x00\x00\x00"
        "\x00\x00\x00\x05" // total body
        "\x12\x34\x56\x78" // opaque
        "\x00\x00\x00\x00\x00\x00\x00\x10" // CAS
        "Hello" // key
        );
    cybozu_assert( r1.length() != 0 );
    cybozu_assert( r1.status() == binary_status::OK );
    cybozu_assert( r1.command() == binary_command::Delete );
    cybozu_assert( ! r1.quiet() );
    ITEMCMP(r1.key(), "Hello");
    REQ(2, "\x80\x14\x00\x05\x00\x00\x00\x00"
        "\x00\x00\x00\x05" // total body
        "\x12\x34\x56\x78" // opaque
        "\x00\x00\x00\x00\x00\x00\x00\x10" // CAS
        "Hello" // key
        );
    cybozu_assert( r2.length() != 0 );
    cybozu_assert( r2.status() == binary_status::OK );
    cybozu_assert( r2.command() == binary_command::DeleteQ );
    cybozu_assert( r2.quiet() );
    REQ(3, "\x80\x14\x00\x00\x00\x00\x00\x00"
        "\x00\x00\x00\x00" // total body
        "\x12\x34\x56\x78" // opaque
        "\x00\x00\x00\x00\x00\x00\x00\x10" // CAS
        );
    cybozu_assert( r3.length() != 0 );
    cybozu_assert( r3.status() == binary_status::Invalid );
}

AUTOTEST(inc_dec) {
    REQ(1, "\x80\x05\x00\x03\x14\x00\x00\x00"
        "\x00\x00\x00\x17" // total body
        "\x12\x34\x56\x78" // opaque
        "\x00\x00\x00\x00\x00\x00\x00\x10" // CAS
        "\x00\x00\x00\x00\x00\x00\x00\x20" // value
        "\x00\x00\x00\x00\x00\x00\x00\x01" // inital
        "\xff\xff\xff\xff" // extra (exptime)
        "abc" // key
        );
    cybozu_assert( r1.length() == (24 + 23) );
    cybozu_assert( r1.status() == binary_status::OK );
    cybozu_assert( r1.command() == binary_command::Increment );
    cybozu_assert( ! r1.quiet() );
    cybozu_assert( r1.value() == 0x20 );
    cybozu_assert( r1.initial() == 0x01 );
    cybozu_assert( r1.exptime() == binary_request::EXPTIME_NONE );
    REQ(2, "\x80\x06\x00\x03\x14\x00\x00\x00"
        "\x00\x00\x00\x17" // total body
        "\x12\x34\x56\x78" // opaque
        "\x00\x00\x00\x00\x00\x00\x00\x10" // CAS
        "\x00\x00\x00\x00\x00\x00\x00\x20" // value
        "\x00\x00\x00\x00\x00\x00\x00\x01" // inital
        "\x00\x00\x00\x00" // extra (exptime)
        "abc" // key
        );
    cybozu_assert( r2.length() == (24 + 23) );
    cybozu_assert( r2.status() == binary_status::OK );
    cybozu_assert( r2.command() == binary_command::Decrement );
    cybozu_assert( ! r2.quiet() );
    cybozu_assert( r2.exptime() == 0 );
    REQ(3, "\x80\x06\x00\x03\x0c\x00\x00\x00"
        "\x00\x00\x00\x0f" // total body
        "\x12\x34\x56\x78" // opaque
        "\x00\x00\x00\x00\x00\x00\x00\x10" // CAS
        "\x00\x00\x00\x00\x00\x00\x00\x20" // value
        "\x00\x00\x00\x00" // extra (exptime)
        "abc" // key
        );
    cybozu_assert( r3.length() != 0 );
    cybozu_assert( r3.status() == binary_status::Invalid );
    REQ(4, "\x80\x15\x00\x03\x14\x00\x00\x00"
        "\x00\x00\x00\x17" // total body
        "\x12\x34\x56\x78" // opaque
        "\x00\x00\x00\x00\x00\x00\x00\x10" // CAS
        "\x00\x00\x00\x00\x00\x00\x00\x20" // value
        "\x00\x00\x00\x00\x00\x00\x00\x01" // inital
        "\x00\x00\x00\x00" // extra (exptime)
        "abc" // key
        );
    cybozu_assert( r4.length() != 0 );
    cybozu_assert( r4.status() == binary_status::OK );
    cybozu_assert( r4.command() == binary_command::IncrementQ );
    cybozu_assert( r4.quiet() );
    REQ(5, "\x80\x16\x00\x03\x14\x00\x00\x00"
        "\x00\x00\x00\x17" // total body
        "\x12\x34\x56\x78" // opaque
        "\x00\x00\x00\x00\x00\x00\x00\x10" // CAS
        "\x00\x00\x00\x00\x00\x00\x00\x20" // value
        "\x00\x00\x00\x00\x00\x00\x00\x01" // inital
        "\x00\x00\x00\x00" // extra (exptime)
        "abc" // key
        );
    cybozu_assert( r5.length() != 0 );
    cybozu_assert( r5.status() == binary_status::OK );
    cybozu_assert( r5.command() == binary_command::DecrementQ );
    cybozu_assert( r5.quiet() );
}

AUTOTEST(flush) {
    REQ(1, "\x80\x08\x00\x00\x00\x00\x00\x00"
        "\x00\x00\x00\x00" // total body
        "\x12\x34\x56\x78" // opaque
        "\x00\x00\x00\x00\x00\x00\x00\x10" // CAS
        );
    cybozu_assert( r1.length() != 0 );
    cybozu_assert( r1.status() == binary_status::OK );
    cybozu_assert( r1.command() == binary_command::Flush );
    cybozu_assert( ! r1.quiet() );
    cybozu_assert( r1.exptime() != 0 ||
                   r1.exptime() != binary_request::EXPTIME_NONE );
    REQ(2, "\x80\x08\x00\x00\x04\x00\x00\x00"
        "\x00\x00\x00\x04" // total body
        "\x12\x34\x56\x78" // opaque
        "\x00\x00\x00\x00\x00\x00\x00\x10" // CAS
        "\x11\x22\x33\x44" // extra (exptime)
        );
    cybozu_assert( r2.length() != 0 );
    cybozu_assert( r2.status() == binary_status::OK );
    cybozu_assert( r2.command() == binary_command::Flush );
    cybozu_assert( r2.exptime() == 0x11223344UL );
    REQ(3, "\x80\x18\x00\x00\x00\x00\x00\x00"
        "\x00\x00\x00\x00" // total body
        "\x12\x34\x56\x78" // opaque
        "\x00\x00\x00\x00\x00\x00\x00\x10" // CAS
        );
    cybozu_assert( r3.length() != 0 );
    cybozu_assert( r3.status() == binary_status::OK );
    cybozu_assert( r3.command() == binary_command::FlushQ );
    cybozu_assert( r3.quiet() );
}

AUTOTEST(append_prepend) {
    REQ(1, "\x80\x0e\x00\x05\x00\x00\x00\x00"
        "\x00\x00\x00\x06" // total body
        "\x12\x34\x56\x78" // opaque
        "\x00\x00\x00\x00\x00\x00\x00\x10" // CAS
        "Hello" // key
        "!" // body
        );
    cybozu_assert( r1.length() != 0 );
    cybozu_assert( r1.status() == binary_status::OK );
    cybozu_assert( r1.command() == binary_command::Append );
    cybozu_assert( ! r1.quiet() );
    ITEMCMP(r1.key(), "Hello");
    ITEMCMP(r1.data(), "!");
    REQ(2, "\x80\x0f\x00\x05\x00\x00\x00\x00"
        "\x00\x00\x00\x08" // total body
        "\x12\x34\x56\x78" // opaque
        "\x00\x00\x00\x00\x00\x00\x00\x10" // CAS
        "Hello" // key
        "!!!" // body
        );
    cybozu_assert( r2.length() != 0 );
    cybozu_assert( r2.status() == binary_status::OK );
    cybozu_assert( r2.command() == binary_command::Prepend );
    cybozu_assert( ! r2.quiet() );
    ITEMCMP(r2.key(), "Hello");
    ITEMCMP(r2.data(), "!!!");
    REQ(3, "\x80\x19\x00\x05\x00\x00\x00\x00"
        "\x00\x00\x00\x08" // total body
        "\x12\x34\x56\x78" // opaque
        "\x00\x00\x00\x00\x00\x00\x00\x10" // CAS
        "Hello" // key
        "!!!" // body
        );
    cybozu_assert( r3.length() != 0 );
    cybozu_assert( r3.status() == binary_status::OK );
    cybozu_assert( r3.command() == binary_command::AppendQ );
    cybozu_assert( r3.quiet() );
    REQ(4, "\x80\x1a\x00\x05\x00\x00\x00\x00"
        "\x00\x00\x00\x08" // total body
        "\x12\x34\x56\x78" // opaque
        "\x00\x00\x00\x00\x00\x00\x00\x10" // CAS
        "Hello" // key
        "!!!" // body
        );
    cybozu_assert( r4.length() != 0 );
    cybozu_assert( r4.status() == binary_status::OK );
    cybozu_assert( r4.command() == binary_command::PrependQ );
    cybozu_assert( r4.quiet() );
}

AUTOTEST(stat) {
    REQ(1, "\x80\x10\x00\x03\x00\x00\x00\x00"
        "\x00\x00\x00\x03" // total body
        "\x12\x34\x56\x78" // opaque
        "\x00\x00\x00\x00\x00\x00\x00\x10" // CAS
        "foo" // key
        );
    cybozu_assert( r1.length() != 0 );
    cybozu_assert( r1.status() == binary_status::OK );
    cybozu_assert( r1.command() == binary_command::Stat );
    cybozu_assert( ! r1.quiet() );
    ITEMCMP(r1.key(), "foo");
    cybozu_assert( r1.stats() == stats_t::GENERAL );
    REQ(2, "\x80\x10\x00\x00\x00\x00\x00\x00"
        "\x00\x00\x00\x00" // total body
        "\x12\x34\x56\x78" // opaque
        "\x00\x00\x00\x00\x00\x00\x00\x10" // CAS
        );
    cybozu_assert( r2.length() != 0 );
    cybozu_assert( r2.status() == binary_status::OK );
    cybozu_assert( r2.stats() == stats_t::GENERAL );
    REQ(3, "\x80\x10\x00\x05\x00\x00\x00\x00"
        "\x00\x00\x00\x05" // total body
        "\x12\x34\x56\x78" // opaque
        "\x00\x00\x00\x00\x00\x00\x00\x10" // CAS
        "items" // key
        );
    cybozu_assert( r3.length() != 0 );
    cybozu_assert( r3.status() == binary_status::OK );
    cybozu_assert( r3.stats() == stats_t::ITEMS );
    REQ(4, "\x80\x10\x00\x05\x00\x00\x00\x00"
        "\x00\x00\x00\x05" // total body
        "\x12\x34\x56\x78" // opaque
        "\x00\x00\x00\x00\x00\x00\x00\x10" // CAS
        "sizes" // key
        );
    cybozu_assert( r4.length() != 0 );
    cybozu_assert( r4.status() == binary_status::OK );
    cybozu_assert( r4.stats() == stats_t::SIZES );
    REQ(5, "\x80\x10\x00\x08\x00\x00\x00\x00"
        "\x00\x00\x00\x08" // total body
        "\x12\x34\x56\x78" // opaque
        "\x00\x00\x00\x00\x00\x00\x00\x10" // CAS
        "settings" // key
        );
    cybozu_assert( r5.length() != 0 );
    cybozu_assert( r5.status() == binary_status::OK );
    cybozu_assert( r5.stats() == stats_t::SETTINGS );
}

AUTOTEST(touch) {
    REQ(1, "\x80\x1c\x00\x05\x04\x00\x00\x00"
        "\x00\x00\x00\x09" // total body
        "\x12\x34\x56\x78" // opaque
        "\x00\x00\x00\x00\x00\x00\x00\x01" // CAS
        "\x00\x00\x01\x00" // extra (exptime)
        "Hello " // key
        );
    cybozu_assert( r1.length() == (24 + 9) );
    cybozu_assert( r1.status() == binary_status::OK );
    cybozu_assert( r1.command() == binary_command::Touch );
    cybozu_assert( r1.exptime() != 0 &&
                   r1.exptime() != binary_request::EXPTIME_NONE );
    REQ(2, "\x80\x1c\x00\x05\x00\x00\x00\x00"
        "\x00\x00\x00\x05" // total body
        "\x12\x34\x56\x78" // opaque
        "\x00\x00\x00\x00\x00\x00\x00\x01" // CAS
        "Hello " // key
        );
    cybozu_assert( r2.length() != 0 );
    cybozu_assert( r2.status() == binary_status::Invalid );
}

AUTOTEST(misc) {
    REQ(1, "\x80\x07\x00\x00\x00\x00\x00\x00"
        "\x00\x00\x00\x00" // total body
        "\x12\x34\x56\x78" // opaque
        "\x00\x00\x00\x00\x00\x00\x00\x10" // CAS
        );
    cybozu_assert( r1.length() != 0 );
    cybozu_assert( r1.status() == binary_status::OK );
    cybozu_assert( r1.command() == binary_command::Quit );
    cybozu_assert( ! r1.quiet() );
    REQ(2, "\x80\x17\x00\x00\x00\x00\x00\x00"
        "\x00\x00\x00\x00" // total body
        "\x12\x34\x56\x78" // opaque
        "\x00\x00\x00\x00\x00\x00\x00\x10" // CAS
        );
    cybozu_assert( r2.length() != 0 );
    cybozu_assert( r2.status() == binary_status::OK );
    cybozu_assert( r2.command() == binary_command::QuitQ );
    cybozu_assert( r2.quiet() );
    REQ(3, "\x80\x0a\x00\x00\x00\x00\x00\x00"
        "\x00\x00\x00\x00" // total body
        "\x12\x34\x56\x78" // opaque
        "\x00\x00\x00\x00\x00\x00\x00\x10" // CAS
        );
    cybozu_assert( r3.length() != 0 );
    cybozu_assert( r3.status() == binary_status::OK );
    cybozu_assert( r3.command() == binary_command::Noop );
    REQ(4, "\x80\x0b\x00\x00\x00\x00\x00\x00"
        "\x00\x00\x00\x00" // total body
        "\x12\x34\x56\x78" // opaque
        "\x00\x00\x00\x00\x00\x00\x00\x10" // CAS
        );
    cybozu_assert( r4.length() != 0 );
    cybozu_assert( r4.status() == binary_status::OK );
    cybozu_assert( r4.command() == binary_command::Version );
}
