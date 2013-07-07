#include <cybozu/ip_address.hpp>

#include <iostream>

int main(int argc, char** argv) {
    if( argc < 2 ) {
        std::cout << "Usage: netiface ADDRESS" << std::endl;
        return 0;
    }
    cybozu::ip_address a(argv[1]);
    if( a.is_v4() ) {
        std::cout << "v4!" << std::endl;
    } else if( a.is_v6() ) {
        std::cout << "v6!" << std::endl;
    }
    cybozu::ip_address b("127.0.0.1");
    if( a == b ) {
        std::cout << a.str() << " == 127.0.0.1" << std::endl;
    } else {
        std::cout << a.str() << " != 127.0.0.1" << std::endl;
    }

    if( has_ip_address(a) ) {
        std::cout << a.str() << " is assigned." << std::endl;
    } else {
        std::cout << a.str() << " is not assigned." << std::endl;
    }
    return 0;
}
