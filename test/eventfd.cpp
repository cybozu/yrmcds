#include <cerrno>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <system_error>
#include <sys/eventfd.h>
#include <thread>
#include <unistd.h>

void write_event(int efd, std::uint64_t ev) {
    ssize_t n = ::write(efd, &ev, sizeof(ev));
    if( n != sizeof(ev) )
        throw std::system_error(errno, std::system_category(), "write");
    std::cerr << "write event of " << n << " bytes." << std::endl;
}

std::uint64_t read_event(int efd) {
    std::uint64_t i;
    ssize_t n = ::read(efd, &i, sizeof(i));
    if( n != sizeof(i) )
        throw std::system_error(errno, std::system_category(), "read");
    return i;
}

void foo(int efd) {
    std::cout << "got event: " << read_event(efd) << std::endl;

    // see what will be returned, 10 or 16?  Both can be seen.
    std::cout << "got event: " << read_event(efd) << std::endl;
}

int main(int argc, char** argv) {
    int efd = eventfd(0, EFD_CLOEXEC);
    if( efd == -1 )
        throw std::system_error(errno, std::system_category(), "eventfd");

    write_event(efd, 2);

    std::thread t(foo, efd);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    write_event(efd, 10);
    write_event(efd, 6);
    t.join();
    return 0;
}
