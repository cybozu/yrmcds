#define TEST_DISABLE_AUTO_RUN
#include "../src/semaphore/semaphore.hpp"
#include "semaphore_client.hpp"

using namespace semaphore_client;

const char* g_server;
uint16_t g_port;

int connect_server() {
    int s = cybozu::tcp_connect(g_server, g_port);
    if( s == -1 ) return -1;
    ::fcntl(s, F_SETFL, ::fcntl(s, F_GETFL, 0) & ~O_NONBLOCK);
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 300000;
    ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    int ok = 1;
    ::setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &ok, sizeof(ok));
    return s;
}

#define ASSERT_RESPONSE(c, r, s, cmd) do { \
        cybozu_assert( c.recv(r) ); \
        cybozu_assert( r.opaque() == s ); \
        cybozu_assert( r.command() == yrmcds::semaphore::command::cmd ); \
    } while( false )

AUTOTEST(interactive) {
    client c(connect_server());
    response r;
    serial_t s;

    while( ! std::cin.eof() ) {
        std::cout << "Command: " << std::flush;
        std::string cmd;
        std::cin >> cmd;
        if( cmd == "noop" ) {
            s = c.noop();
            ASSERT_RESPONSE(c, r, s, Noop);
            if( r.status() == yrmcds::semaphore::status::OK )
                std::cout << "OK" << std::endl;
            else
                std::cout << r.message() << std::endl;
        }
        else if( cmd == "get" ) {
            std::string name;
            std::cin >> name;
            s = c.get(name);
            ASSERT_RESPONSE(c, r, s, Get);
            if( r.status() == yrmcds::semaphore::status::OK )
                std::cout << r.available() << std::endl;
            else
                std::cout << r.message() << std::endl;
        }
        else if( cmd == "acquire" ) {
            std::string name;
            std::cin >> name;
            std::uint32_t resources, initial;
            std::cin >> resources >> initial;
            s = c.acquire(name, resources, initial);
            ASSERT_RESPONSE(c, r, s, Acquire);
            if( r.status() == yrmcds::semaphore::status::OK )
                std::cout << r.resources() << std::endl;
            else
                std::cout << r.message() << std::endl;
        }
        else if( cmd == "release" ) {
            std::string name;
            std::cin >> name;
            std::uint32_t resources;
            std::cin >> resources;
            s = c.release(name, resources);
            ASSERT_RESPONSE(c, r, s, Release);
            if( r.status() == yrmcds::semaphore::status::OK )
                std::cout << "OK" << std::endl;
            else
                std::cout << r.message() << std::endl;
        }
        else if( cmd == "stats" ) {
            s = c.stats();
            ASSERT_RESPONSE(c, r, s, Stats);
            if( r.status() == yrmcds::semaphore::status::OK ) {
                for( auto& stat: r.stats() )
                    std::cout << stat.name << ": " << stat.value << std::endl;
                std::cout << "END" << std::endl;
            } else {
                std::cout << r.message() << std::endl;
            }
        }

        if( std::cin.fail() ) {
            std::cin.clear();
            std::cin.ignore();
        }
    }
}

// main
bool optparse(int argc, char** argv) {
    if( argc != 2 && argc != 3 ) {
        std::cout << "Usage: semaphore_interactive.exe SERVER [PORT]" << std::endl;
        return false;
    }
    g_server = argv[1];
    g_port = 11215;
    if( argc == 3 ) {
        int n = std::stoi(argv[2]);
        if( n <= 0 || n > 65535 ) {
            std::cout << "Invalid port number: " << argv[2] << std::endl;
            return false;
        }
        g_port = n;
    }

    int s = connect_server();
    if( s == -1 ) {
        std::cout << "Failed to connect to " << g_server << ":" << g_port << std::endl;
        return false;
    }
    ::close(s);
    return true;
}

TEST_MAIN(optparse);
