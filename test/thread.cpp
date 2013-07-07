#include <cybozu/thread.hpp>

#include <iostream>
#include <mutex>
#include <memory>
#include <stdexcept>
#include <thread>

struct ttt: public cybozu::thread_base<ttt, 0> {
    ttt(): cybozu::thread_base<ttt, 0>() {}
    int i = 3;

    void run() {
        std::cout << "i = " << i << std::endl;
        throw std::runtime_error("hoge");
    }

    void stop() {
        m_thread.join();
    }
};

int main() {
    std::unique_ptr<int> p;
    std::once_flag f;
    std::call_once(f, [&]{
            p.reset(new int);
        });
    std::cout << "*p=" << *p << std::endl;

    ttt t;
    t.start();
    t.stop();
    return 0;
}
