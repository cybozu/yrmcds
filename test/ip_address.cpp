#include <cybozu/ip_address.hpp>
#include <cybozu/test.hpp>

AUTOTEST(ipv4) {
    cybozu_test_no_exception(
        cybozu::ip_address("123.123.123.0")
    );

    cybozu_test_exception(
        cybozu::ip_address("123.456.789.0"), cybozu::ip_address::bad_address
    );

    cybozu_test_exception(
        cybozu::ip_address("localhost"), cybozu::ip_address::bad_address
    );

    cybozu::ip_address ipv4("123.123.123.0");
    cybozu_assert( ipv4.is_v4() );
    cybozu_assert( ipv4.str() == "123.123.123.0" );
}

AUTOTEST(ipv6) {
    cybozu_test_no_exception(
        cybozu::ip_address("::1")
    );

    // cybozu_test_no_exception(
    //     cybozu::ip_address("fe80::dead:beaf%lo")
    // );

    cybozu_test_exception(
        cybozu::ip_address("fg::1"), cybozu::ip_address::bad_address
    );

    cybozu_test_exception(
        cybozu::ip_address("::1%lo"), cybozu::ip_address::bad_address
    );

    cybozu::ip_address ipv6("fd00:1234::dead:beaf");
    cybozu_assert( ipv6.is_v6() );
    cybozu_assert( ipv6.str() == "fd00:1234::dead:beaf" );
}

AUTOTEST(compare) {
    using cybozu::ip_address;
    cybozu_assert( ip_address("127.0.0.1") == ip_address("127.0.0.1") );
    cybozu_assert( ip_address("127.0.0.1") != ip_address("162.193.0.1") );
    cybozu_assert( ip_address("127.0.0.1") != ip_address("::1") );
    cybozu_assert( ip_address("::1") == ip_address("::1") );
    // cybozu_assert( ip_address("fe80::1%lo") == ip_address("fe80::1%lo") );
    // cybozu_assert( ip_address("fe80::1%lo") != ip_address("fe80::1%eth0") );
}

AUTOTEST(has_ip_address) {
    for( int i = 0; i < 1000; ++i )
        cybozu::has_ip_address(cybozu::ip_address("11.11.11.11"));
}
