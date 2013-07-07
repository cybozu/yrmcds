#include <cybozu/ip_address.hpp>
#include <cybozu/test.hpp>

AUTOTEST(has_ip_address) {
    for( int i = 0; i < 1000; ++i )
        cybozu::has_ip_address(cybozu::ip_address("11.11.11.11"));
}
