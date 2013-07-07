#include <cybozu/logger.hpp>

/* To see the optimization effects, 
 * 1. run gdbtui,
 * 2. enter "layout asm",
 * 3. "b main", "run", then "stepi".
 */

int main(int argc, char** argv) {
    if( argc > 1 )
        cybozu::logger::instance().open(argv[1]);
    cybozu::logger::debug() << "hoge, " << 3.145 << ", " << (10LL<<40);
    cybozu::logger::info() << "fuga";
    return 0;
}
