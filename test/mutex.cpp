#include <cybozu/test.hpp>

#include <mutex>
#include <thread>
#include <iostream>

std::mutex lock;

int g_counter = 0;

void foo() {
    for (int i = 0; i < 10000; ++i) {
        std::lock_guard<std::mutex> g(lock);
        ++g_counter;
    }
    return;
}

AUTOTEST(mutex) {
    std::thread t1(foo);
    std::thread t2(foo);
    std::thread t3(foo);
    foo();
    t1.join();
    t2.join();
    t3.join();
    cybozu_assert( g_counter == 40000 );
}
