// (C) 2013 Cybozu.

#include "reactor.hpp"
#include "util.hpp"

#include <algorithm>
#include <chrono>
#include <iterator>

namespace {

const int EPOLL_SIZE = 128;
const int POLLING_TIMEOUT = 100; // milli seconds

}

namespace cybozu {

reactor::reactor():
    m_fd( epoll_create1(EPOLL_CLOEXEC) ), m_running(true)
{
    if( m_fd == -1 )
        throw_unix_error(errno, "epoll_create1");
    m_resources.max_load_factor(1.0);
    m_resources.reserve(100000);
    m_readables.reserve(256);
    m_readables_copy.reserve(256);
    m_drop_req.reserve(256);
    m_drop_req_copy.reserve(256);
}

reactor::~reactor() {
    ::close(m_fd);
}

void reactor::add_resource(std::unique_ptr<resource> res, int events) {
    if( res->m_reactor != nullptr )
        throw std::logic_error("<reactor::add_resource> already added!");
    res->m_reactor = this;
    const int fd = res->fileno();
    struct epoll_event ev;
    ev.events = events | EPOLLET;
    ev.data.fd = fd;
    if( epoll_ctl(m_fd, EPOLL_CTL_ADD, fd, &ev) == -1 )
        throw_unix_error(errno, "epoll_ctl");
    m_resources.emplace(fd, std::move(res));
}

void reactor::modify_events(const resource& res, int events) {
    const int fd = res.fileno();
    struct epoll_event ev;
    ev.events = events | EPOLLET;
    ev.data.fd = fd;
    if( epoll_ctl(m_fd, EPOLL_CTL_MOD, fd, &ev) == -1 )
        throw_unix_error(errno, "epoll_ctl");
}

void reactor::run(std::function<void(reactor& r)> callback, int interval) {
    namespace crn = std::chrono;
    const int milli_interval = interval * 1000;
    auto last = crn::steady_clock::now();

    m_running = true;
    while( m_running ) {
        poll();
        auto now = crn::steady_clock::now();
        auto diff = crn::duration_cast<crn::milliseconds>(now - last).count();
        if( diff > milli_interval ) {
            callback(*this);
            last = now;
        }
    }
}

void reactor::remove_resource(int fd) {
    resource_map::iterator it = m_resources.find(fd);
    if( it == m_resources.end() ) {
        dump_stack();
        throw std::logic_error("bug in remove_resource");
    }
    m_garbage.emplace_back( std::move(it->second) );
    m_resources.erase(it);
    if( epoll_ctl(m_fd, EPOLL_CTL_DEL, fd, NULL) == -1 )
        throw_unix_error(errno, "epoll_ctl(EPOLL_CTL_DEL)");
    m_readables.erase(std::remove(m_readables.begin(), m_readables.end(), fd),
                      m_readables.end());
}

void reactor::poll() {
    // process readable resources
    std::sort(m_readables.begin(), m_readables.end());
    std::unique_copy(m_readables.begin(), m_readables.end(),
                     std::back_inserter(m_readables_copy));
    m_readables.clear();
    for( int fd: m_readables_copy ) {
        if( ! m_resources[fd]->on_readable() )
            remove_resource(fd);
    }
    m_readables_copy.clear();

    // process drop requests
    {
        lock_guard g(m_lock);
        m_drop_req_copy.swap(m_drop_req);
    }
    for( int fd: m_drop_req_copy )
        remove_resource(fd);
    m_drop_req_copy.clear();

    struct epoll_event events[EPOLL_SIZE];
    int timeout = m_readables.empty() ? POLLING_TIMEOUT : 0;
    int n = epoll_wait(m_fd, events, EPOLL_SIZE, timeout);
    if( n == -1 ) {
        if( errno == EINTR ) return;
        throw_unix_error(errno, "epoll_wait");
    }

    for( int i = 0; i < n; ++i ) {
        const struct epoll_event& ev = events[i];
        const int fd = ev.data.fd;
        resource& r = *(m_resources[fd]);
        if( ev.events & EPOLLERR ) {
            if( ! r.on_error() )
                remove_resource(fd);
            continue;
        }
        if( ev.events & EPOLLHUP ) {
            if( ! r.on_hangup() )
                remove_resource(fd);
            continue;
        }
        if( ev.events & EPOLLIN ) {
            if( ! r.on_readable() ) {
                remove_resource(fd);
                continue;
            }
        }
        if( ev.events & EPOLLOUT ) {
            if( ! r.on_writable() )
                remove_resource(fd);
        }
    }
}

} // namespace cybozu
