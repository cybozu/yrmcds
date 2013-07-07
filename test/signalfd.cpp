#include <thread>
#include <chrono>
#include <iostream>
#include <unistd.h>
#include <signal.h>
#include <cstring>
#include <sys/signalfd.h>
#include <sys/epoll.h>
#include <cstddef>
#include <stdexcept>

const int MAX_EVENTS = 10;

bool readall(int fd, char* p, std::size_t len) {
    while( len != 0 ) {
        ssize_t n = read(fd, p, len);
        if( n == -1 ) {
            if( errno == EAGAIN || errno == EWOULDBLOCK ) {
                return false;
            } else {
                throw std::runtime_error("read failed.");
            }
        }
        len -= n;
        p += n;
    }
    return true;
}

void read_signal(int sfd) {
    struct signalfd_siginfo si;
    while( true ) {
        if( ! readall(sfd, (char*)&si, sizeof(si)) )
            return;
        switch(si.ssi_signo) {
        case SIGHUP:
            std::cout << "SIGHUP" << std::endl;
            break;
        case SIGQUIT:
            std::cout << "SIGQUIT" << std::endl;
            break;
        case SIGTERM:
            std::cout << "SIGTERM" << std::endl;
            break;
        case SIGCHLD:
            std::cout << "SIGCHLD" << std::endl;
            break;
        default:
            std::cout << "Unknown signal: " << si.ssi_signo << std::endl;
        }
    }
}

int wait_signal(int sfd) {
    int efd = epoll_create(10);
    if( efd == -1 ) {
        std::cerr << "epoll_create failed." << std::endl;
        close(efd);
        return 1;
    }
    struct epoll_event ev;
    ev.events = EPOLLIN|EPOLLET;
    ev.data.fd = sfd;
    if( epoll_ctl(efd, EPOLL_CTL_ADD, sfd, &ev) == -1 ) {
        std::cerr << "epoll_ctl failed." << std::endl;
        close(efd);
        return 1;
    }

    std::cerr << "pid=" << getpid() << std::endl;
    std::cerr << "waiting for signals..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(10));

    struct epoll_event events[MAX_EVENTS];
    int n = epoll_wait(efd, events, MAX_EVENTS, -1);
    if( n == -1 ) {
        std::cerr << "epoll_wait failed." << std::endl;
        close(efd);
        return 1;
    }
    for( int i = 0; i < n; ++i ) {
        if( events[i].data.fd != sfd ) {
            std::cerr << "wtf!?" << std::endl;
            close(efd);
            return 1;
        }
        read_signal(sfd);
    }
    close(efd);
    return 0;
}

int main(int argc, char** argv) {
    if( argc == 1 ) return 0;

    // block signals and setup signalfd
    // Threads created by the main thread inherit the signal mask.
    sigset_t sigs[1];
    sigemptyset(sigs);
    sigaddset(sigs, SIGQUIT);
    sigaddset(sigs, SIGHUP);
    sigaddset(sigs, SIGTERM);
    sigaddset(sigs, SIGCHLD);
    if( pthread_sigmask(SIG_BLOCK, sigs, NULL) != 0 ) {
        std::cerr << "pthread_sigmask failed." << std::endl;
        return 1;
    }
    int sfd = signalfd(-1, sigs, SFD_NONBLOCK);
    if( sfd == -1 ) {
        std::cerr << "signalfd failed." << std::endl;
        return 1;
    }

    // ignore SIGPIPE
    // As per man 7 signal, the signal disposition is a per-process attribute.
    struct sigaction act;
    std::memset(&act, 0, sizeof(act));
    act.sa_handler = SIG_IGN;
    if( sigaction(SIGPIPE, &act, NULL) == -1 ) {
        std::cerr << "sigaction failed." << std::endl;
        return 1;
    }

    int ret = wait_signal(sfd);
    close(sfd);
    return ret;
}
