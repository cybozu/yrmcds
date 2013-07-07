#include <cybozu/config_parser.hpp>

#include <iostream>

int main(int argc, char** argv) {
    cybozu::config_parser cp;
    cp.set("aaa", "3");
    std::cout << cp.get_as_int("aaa") << std::endl;
    cp.set("aaa", "5");
    std::cout << cp.get_as_int("aaa") << std::endl;
    cp.set("bbb", "false");
    std::cout << cp.get_as_bool("bbb") << std::endl;

    if( argc == 2 ) {
        cp.load(argv[1]);
        std::cout << cp.get_as_int("memory_limit") << std::endl;
        std::cout << cp.get("temp_dir") << std::endl;
    }
    return 0;
}
