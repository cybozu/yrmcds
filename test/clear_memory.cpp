#include <cybozu/util.hpp>
#include <cybozu/test.hpp>

AUTOTEST(clear_memory) {
    char hoge[] = "abcdef";
    cybozu::clear_memory(hoge, sizeof(hoge));
    for( auto i = sizeof(hoge); i > 0; --i ) {
        cybozu_assert(hoge[i] == '\0');
    }
}
