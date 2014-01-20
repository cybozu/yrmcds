#define TEST_DISABLE_AUTO_RUN
#include "../src/semaphore/semaphore.hpp"
#include "semaphore_client.hpp"

#include <cybozu/test.hpp>
#include <cybozu/util.hpp>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>
#include <vector>

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

#define ASSERT_RESPONSE(c, r, s, cmd, st) do { \
        cybozu_assert( c.recv(r) ); \
        cybozu_assert( r.opaque() == s ); \
        cybozu_assert( r.command() == yrmcds::semaphore::command::cmd ); \
        cybozu_assert( r.status() == yrmcds::semaphore::status::st ); \
    } while( false )

AUTOTEST(noop) {
    client c(connect_server());
    response r;
    serial_t s;

    // send noop 3 times
    for( int i = 0; i < 3; ++i ) {
        s = c.noop();
        ASSERT_RESPONSE(c, r, s, Noop, OK);
    }
}

AUTOTEST(one_client) {
    client c(connect_server());
    response r;
    serial_t s;

    s = c.acquire("hoge", 2, 10);
    ASSERT_RESPONSE(c, r, s, Acquire, OK);
    cybozu_assert(r.resources() == 2);

    s = c.get("hoge");
    ASSERT_RESPONSE(c, r, s, Get, OK);
    cybozu_assert(r.available() == 8);

    s = c.acquire("hoge", 3, 10);
    ASSERT_RESPONSE(c, r, s, Acquire, OK);
    cybozu_assert(r.resources() == 3);

    s = c.get("hoge");
    ASSERT_RESPONSE(c, r, s, Get, OK);
    cybozu_assert(r.available() == 5);

    s = c.acquire("hoge", 5, 10);
    ASSERT_RESPONSE(c, r, s, Acquire, OK);
    cybozu_assert(r.resources() == 5);

    s = c.get("hoge");
    ASSERT_RESPONSE(c, r, s, Get, OK);
    cybozu_assert(r.available() == 0);

    s = c.acquire("hoge", 1, 10);
    ASSERT_RESPONSE(c, r, s, Acquire, ResourceNotAvailable);

    s = c.release("hoge", 3);
    ASSERT_RESPONSE(c, r, s, Release, OK);

    s = c.acquire("hoge", 2, 10);
    ASSERT_RESPONSE(c, r, s, Acquire, OK);
    cybozu_assert(r.resources() == 2);
}

AUTOTEST(multi_clients) {
    client c1(connect_server());
    response r1;
    serial_t s1;

    {
        client c2(connect_server());
        response r2;
        serial_t s2;

        s1 = c1.acquire("fuga", 1, 10);
        ASSERT_RESPONSE(c1, r1, s1, Acquire, OK);

        s2 = c2.acquire("fuga", 9, 10);
        ASSERT_RESPONSE(c2, r2, s2, Acquire, OK);

        s1 = c1.acquire("fuga", 1, 10);
        ASSERT_RESPONSE(c1, r1, s1, Acquire, ResourceNotAvailable);

        s2 = c2.release("fuga", 1);
        ASSERT_RESPONSE(c2, r2, s2, Release, OK);

        s1 = c1.acquire("fuga", 1, 10);
        ASSERT_RESPONSE(c1, r1, s1, Acquire, OK);

        s1 = c1.acquire("fuga", 1, 10);
        ASSERT_RESPONSE(c1, r1, s1, Acquire, ResourceNotAvailable);

        s2 = c2.acquire("fuga", 1, 10);
        ASSERT_RESPONSE(c2, r2, s2, Acquire, ResourceNotAvailable);

        // 8 resources released due to disconnection of c2.
    }
    ::timespec ts = {0, 100 * 1000 * 1000};
    nanosleep(&ts, nullptr);     // wait 100ms

    s1 = c1.acquire("fuga", 8, 10);
    ASSERT_RESPONSE(c1, r1, s1, Acquire, OK);

    s1 = c1.acquire("fuga", 1, 10);
    ASSERT_RESPONSE(c1, r1, s1, Acquire, ResourceNotAvailable);
}

AUTOTEST(over_releasing) {
    client c(connect_server());
    response r;
    serial_t s;

    s = c.acquire("foo", 1, 10);
    ASSERT_RESPONSE(c, r, s, Acquire, OK);

    s = c.release("foo", 2);
    ASSERT_RESPONSE(c, r, s, Release, NotAcquired);
}

AUTOTEST(multi_names) {
    client c(connect_server());
    response r;
    serial_t s;

    s = c.acquire("a", 1, 10);
    ASSERT_RESPONSE(c, r, s, Acquire, OK);

    s = c.acquire("b", 2, 10);
    ASSERT_RESPONSE(c, r, s, Acquire, OK);

    s = c.acquire("c", 4, 10);
    ASSERT_RESPONSE(c, r, s, Acquire, OK);

    s = c.get("a");
    ASSERT_RESPONSE(c, r, s, Get, OK);
    cybozu_assert(r.available() == 9);

    s = c.get("b");
    ASSERT_RESPONSE(c, r, s, Get, OK);
    cybozu_assert(r.available() == 8);

    s = c.get("c");
    ASSERT_RESPONSE(c, r, s, Get, OK);
    cybozu_assert(r.available() == 6);

    s = c.release("a", 1);
    ASSERT_RESPONSE(c, r, s, Release, OK);

    s = c.release("b", 1);
    ASSERT_RESPONSE(c, r, s, Release, OK);

    s = c.release("c", 2);
    ASSERT_RESPONSE(c, r, s, Release, OK);

    s = c.get("a");
    ASSERT_RESPONSE(c, r, s, Get, OK);
    cybozu_assert(r.available() == 10);

    s = c.get("b");
    ASSERT_RESPONSE(c, r, s, Get, OK);
    cybozu_assert(r.available() == 9);

    s = c.get("c");
    ASSERT_RESPONSE(c, r, s, Get, OK);
    cybozu_assert(r.available() == 8);
}

// main
bool optparse(int argc, char** argv) {
    if( argc != 2 && argc != 3 ) {
        std::cout << "Usage: semaphore.exe SERVER [PORT]" << std::endl;
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
