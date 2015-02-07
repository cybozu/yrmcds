#include <cybozu/util.hpp>
#include <cybozu/test.hpp>

AUTOTEST(clear_memory) {
    char hoge[] = "abcdef";
    cybozu::clear_memory(hoge, sizeof(hoge));
    for( char c : hoge ) {
        cybozu_assert(c == '\0');
    }
}
