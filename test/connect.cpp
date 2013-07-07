#include <cybozu/tcp.hpp>

#include <iostream>

int main(int argc, char** argv) {
    if( argc != 3 ) {
        std::cout << "Usage: connect.exe HOST PORT" << std::endl;
        return 0;
    }

    const char* node = argv[1];
    std::uint16_t port = static_cast<std::uint16_t>( std::stoi(argv[2]) );

    int s = cybozu::tcp_connect(node, port, 5);
    if( s == -1 ) {
        std::cout << "failed." << std::endl;
    } else {
        std::cout << "connected." << std::endl;
        close(s);
    }
    return 0;
}
