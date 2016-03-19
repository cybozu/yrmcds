#include <cybozu/config_parser.hpp>
#include <cybozu/test.hpp>

AUTOTEST(config) {
    cybozu::config_parser cp;
    cp.set("aaa", "3");
    cybozu_assert(cp.get_as_int("aaa") == 3);
    cp.set("aaa", "5");
    cybozu_assert(cp.get_as_int("aaa") == 5);
    cp.set("bbb", "false");
    cybozu_assert(cp.get_as_bool("bbb") == false);
}
