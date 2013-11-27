// (C) 2013 Cybozu.

#include "tcp.hpp"
#include "logger.hpp"

#ifdef USE_TCMALLOC
#  include <google/tcmalloc.h>
#  define MALLOC tc_malloc
#  define FREE tc_free
#else
#  include <cstdlib>
#  define MALLOC std::malloc
#  define FREE std::free
#endif

#include <algorithm>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <system_error>
#include <sys/uio.h>

namespace {

const unsigned int MAX_BUFCNT = 100;
const int KEEPALIVE_IDLE = 300;   // 5 min before keep alive probe
const int KEEPALIVE_INTERVAL = 5; // 5 seconds between keep alive probes

} // anonymous namespace

namespace cybozu {

int tcp_connect(const char* node, std::uint16_t port, unsigned int timeout) {
    std::string s_port = std::to_string(port);

    struct addrinfo hint, *res;
    std::memset(&hint, 0, sizeof(hint));
    hint.ai_family = AF_INET;  // prefer IPv4
    hint.ai_socktype = SOCK_STREAM;
    hint.ai_flags = AI_NUMERICSERV|AI_ADDRCONFIG;
    int e = getaddrinfo(node, s_port.c_str(), &hint, &res);
    if( e == EAI_FAMILY || e == EAI_ADDRFAMILY || e == EAI_NODATA ) {
        hint.ai_family = AF_INET6;
        hint.ai_flags |= AI_V4MAPPED;
        e = getaddrinfo(node, s_port.c_str(), &hint, &res);
    }
    if( e == EAI_SYSTEM ) {
        throw_unix_error(errno, "getaddrinfo");
    } else if( e != 0 ) {
        throw std::runtime_error(std::string("getaddrinfo: ") +
                                 gai_strerror(e));
    }

    int s = ::socket(res->ai_family,
                     res->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC,
                     res->ai_protocol);
    if( s == -1 ) {
        freeaddrinfo(res);
        throw_unix_error(errno, "socket");
    }
    e = ::connect(s, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    if( e == 0 ) return s;
    if( errno != EINPROGRESS ) {
        ::close(s);
        throw_unix_error(errno, "connect");
    }

    struct pollfd fds;
    fds.fd = s;
    fds.events = POLLOUT;
    int poll_timeout = timeout ? timeout*1000 : -1;
    int n = ::poll(&fds, 1, poll_timeout);
    if( n == 0 ) { // timeout
        ::close(s);
        return -1;
    }
    if( n == -1 ) {
        ::close(s);
        throw_unix_error(errno, "poll");
    }

    if( fds.revents & (POLLERR|POLLHUP|POLLNVAL) ) {
        ::close(s);
        return -1;
    }
    socklen_t l = sizeof(e);
    if( getsockopt(s, SOL_SOCKET, SO_ERROR, &e, &l) == -1 ) {
        ::close(s);
        throw_unix_error(errno, "getsockopt(SO_ERROR)");
    }
    if( e != 0 ) {
        ::close(s);
        return -1;
    }
    return s;
}

const std::size_t tcp_socket::SENDBUF_SIZE;

tcp_socket::tcp_socket(int fd, unsigned int bufcnt):
    resource(fd) {
    if( bufcnt > MAX_BUFCNT )
        throw std::logic_error("tcp_socket: Too many buffers");
    m_free_buffers.reserve(bufcnt);
    m_pending.reserve(bufcnt);

    int v = 1;
    if( setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &v, sizeof(v)) == -1 )
        throw_unix_error(errno, "setsockopt(SO_KEEPALIVE)");

    v = 1;
    if( setsockopt(fd, IPPROTO_TCP, TCP_CORK, &v, sizeof(v)) == -1 )
        throw_unix_error(errno, "setsockopt(TCP_CORK)");

    v = KEEPALIVE_IDLE;
    if( setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &v, sizeof(v)) == -1 )
        throw_unix_error(errno, "setsockopt(TCP_KEEPIDLE)");

    v = KEEPALIVE_INTERVAL;
    if( setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &v, sizeof(v)) == -1 )
        throw_unix_error(errno, "setsockopt(TCP_KEEPINTVL)");

    for( unsigned int i = 0; i < bufcnt; ++i ) {
        char* p = (char*)MALLOC(SENDBUF_SIZE);
        if( p == NULL ) {
            for( char* p2: m_free_buffers )
                FREE(p2);
            m_free_buffers.clear();
            throw std::runtime_error("tcp_socket: failed to allocate buffers");
        }
        // reserved, hence no throw
        m_free_buffers.push_back(p);
    }
}

void tcp_socket::free_buffers() {
    lock_guard g(m_lock);
    for( auto& t: m_pending ) {
        char* p;
        std::tie(p, std::ignore, std::ignore) = t;
        FREE(p);
    }
    m_pending.clear();
    for( char* p: m_free_buffers ) {
        FREE(p);
    }
    m_free_buffers.clear();
    m_tmpbuf.clear();
    m_tmpbuf.shrink_to_fit();
    m_shutdown = true;
}

bool tcp_socket::_send(const char* p, std::size_t len, lock_guard& g) {
    while( ! can_send(len) ) m_cond_write.wait(g);
    if( m_shutdown ) return false;

    if( m_pending.empty() ) {
        while( len > 0 ) {
            ssize_t n = ::send(m_fd, p, len, 0);
            if( n == -1 ) {
                if( errno == EAGAIN || errno == EWOULDBLOCK ) break;
                if( errno == EINTR ) continue;
                auto ecnd = std::system_category().default_error_condition(errno);
                if( ecnd.value() != EPIPE )
                    logger::error() << "<tcp_socket::_send>: ("
                                    << ecnd.value() << ") "
                                    << ecnd.message();
                g.unlock();
                invalidate_and_close();
                return false;
            }
            p += n;
            len -= n;
        }
        if( len == 0 ) return true;
    }

    // put data in the pending request queue.
    if( capacity() < len ) {
        // here, m_pending.empty() and m_tmpbuf.empty() holds true.
        logger::debug() << "<tcp_socket::_send> buffering "
                        << len << " bytes data.";
        m_tmpbuf.resize(len);
        std::memcpy(m_tmpbuf.data(), p, len);
        return true;
    }

    if( ! m_pending.empty() ) {
        auto& t = m_pending.back();
        char* t_p;
        std::size_t t_len;
        std::tie(t_p, t_len, std::ignore) = t;
        std::size_t room = SENDBUF_SIZE - t_len;
        if( room > 0 ) {
            std::size_t to_write = std::min(room, len);
            std::memcpy(t_p + t_len, p, to_write);
            p += to_write;
            len -= to_write;
            std::get<1>(t) = t_len + to_write;
            if( len == 0 ) return true;
        }
    }

    while( len > 0 ) {
        char* t_p = m_free_buffers.back();
        m_free_buffers.pop_back();
        std::size_t to_write = std::min(len, SENDBUF_SIZE);
        std::memcpy(t_p, p, to_write);
        p += to_write;
        len -= to_write;
        m_pending.emplace_back(t_p, to_write, 0);
    }
    return true;
}

bool tcp_socket::_sendv(const iovec* iov, const int iovcnt, lock_guard& g) {
    std::size_t total = 0;
    for( int i = 0; i < iovcnt; ++i ) {
        total += iov[i].len;
    }

    while( ! can_send(total) ) m_cond_write.wait(g);
    if( m_shutdown ) return false;

    ::iovec v[MAX_IOVCNT];
    for( int i = 0; i < iovcnt; ++i ) {
        v[i].iov_base = (void*)(iov[i].p);
        v[i].iov_len = iov[i].len;
    }
    int ind = 0;

    if( m_pending.empty() ) {
        while( ind < iovcnt ) {
            ssize_t n = ::writev(m_fd, &(v[ind]), iovcnt - ind);
            if( n == -1 ) {
                if( errno == EAGAIN || errno == EWOULDBLOCK ) break;
                if( errno == EINTR ) continue;
                auto ecnd = std::system_category().default_error_condition(errno);
                if( ecnd.value() != EPIPE )
                    logger::error() << "<tcp_socket::_sendv>: ("
                                    << ecnd.value() << ") "
                                    << ecnd.message();
                g.unlock();
                invalidate_and_close();
                return false;
            }
            while( n > 0 ) {
                if( static_cast<std::size_t>(n) < v[ind].iov_len ) {
                    v[ind].iov_base = ((char*)v[ind].iov_base) + n;
                    v[ind].iov_len = v[ind].iov_len - n;
                    break;
                }
                n -= v[ind].iov_len;
                ++ind;
            }
        }
        if( ind == iovcnt ) return true;
    }

    // recalculate total length
    total = 0;
    for( int i = ind; i < iovcnt; ++i ) {
        total += v[i].iov_len;
    }

    // put data in the pending request queue.
    if( capacity() < total ) {
        // here, m_pending.empty() and m_tmpbuf.empty() holds true.
        logger::debug() << "<tcp_socket::_sendv> buffering "
                        << total << " bytes data.";
        m_tmpbuf.resize(total);
        char* p = m_tmpbuf.data();
        for( int i = ind; i < iovcnt; ++i ) {
            std::memcpy(p, v[i].iov_base, v[i].iov_len);
            p += v[i].iov_len;
        }
        return true;
    }

    while( ind < iovcnt ) {
        char* t_p;
        std::size_t t_len;
        if(m_pending.empty() || std::get<1>(m_pending.back()) == SENDBUF_SIZE) {
            t_p = m_free_buffers.back();
            t_len = 0;
            m_free_buffers.pop_back();
            m_pending.emplace_back(t_p, t_len, 0);
        } else {
            std::tie(t_p, t_len, std::ignore) = m_pending.back();
        }
        std::size_t room = SENDBUF_SIZE - t_len;
        while( room > 0 ) {
            std::size_t to_write = std::min(room, v[ind].iov_len);
            std::memcpy(t_p + t_len, v[ind].iov_base, to_write);
            room -= to_write;
            t_len += to_write;
            std::get<1>(m_pending.back()) = t_len;
            if( to_write == v[ind].iov_len ) {
                ++ind;
                if( ind == iovcnt ) break;
                continue;
            }
            v[ind].iov_base = ((char*)v[ind].iov_base) + to_write;
            v[ind].iov_len -= to_write;
        }
    }
    return true;
}

bool tcp_socket::write_pending_data() {
    lock_guard g(m_lock);

    while( ! m_tmpbuf.empty() ) {
        ssize_t n = ::send(m_fd, m_tmpbuf.data(), m_tmpbuf.size(), 0);
        if( n == -1 ) {
            if( errno == EINTR ) continue;
            if( errno == EAGAIN || errno == EWOULDBLOCK ) return true;
            auto ecnd = std::system_category().default_error_condition(errno);
            if( ecnd.value() != EPIPE )
                logger::error() << "<tcp_socket::write_pending_data>: ("
                                << ecnd.value() << ") "
                                << ecnd.message();
            return false;
        }
        m_tmpbuf.erase(m_tmpbuf.begin(), m_tmpbuf.begin() + n);
    }
    m_tmpbuf.shrink_to_fit();

    while( ! m_pending.empty() ) {
        auto& t = m_pending.front();
        char* p;
        std::size_t len;
        std::size_t sent;
        std::tie(p, len, sent) = t;

        while( len != sent ) {
            ssize_t n = ::send(m_fd, p+sent, len-sent, 0);
            if( n == -1 ) {
                if( errno == EINTR ) continue;
                if( errno == EAGAIN || errno == EWOULDBLOCK ) break;
                auto ecnd = std::system_category().default_error_condition(errno);
                logger::error() << "<tcp_socket::write_pending_data>: ("
                                << ecnd.value() << ") "
                                << ecnd.message();
                return false;
            }
            sent += n;
        }
        if( len == sent ) {
            m_pending.erase(m_pending.begin());
            m_free_buffers.push_back(p);
        } else {
            std::get<2>(t) = sent;
            g.unlock();
            // notify other threads of the new free space
            m_cond_write.notify_all();
            return true;
        }
    }

    // all data have been sent.
    _flush();

    if( ! m_shutdown ) {
        g.unlock();
        m_cond_write.notify_all();
        return true;
    }

    // invalidate and close this socket when m_shutdown==true.
    return false;
}

int setup_server_socket(const char* bind_addr, std::uint16_t port) {
    struct addrinfo hint, *res;
    std::string s_port = std::to_string(port);
    std::memset(&hint, 0, sizeof(hint));
    hint.ai_family = AF_INET6; // can accept IPv4 address
    hint.ai_socktype = SOCK_STREAM;
    hint.ai_flags = AI_PASSIVE | AI_NUMERICSERV;
    int e = getaddrinfo(bind_addr, s_port.c_str(), &hint, &res);
    if( e == EAI_FAMILY || e == EAI_ADDRFAMILY || e == EAI_NODATA ) {
        logger::info() << "Binding to IPv6 fails, trying IPv4...";
        hint.ai_family = AF_INET;
        e = getaddrinfo(bind_addr, s_port.c_str(), &hint, &res);
    }
    if( e == EAI_SYSTEM ) {
        throw_unix_error(errno, "getaddrinfo");
    } else if( e != 0 ) {
        throw std::runtime_error(std::string("getaddrinfo: ") +
                                 gai_strerror(e));
    }

    int s = ::socket(res->ai_family,
                     res->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC,
                     res->ai_protocol);
    if( s == -1 ) {
        freeaddrinfo(res);
        throw_unix_error(errno, "socket");
    }

    // intentionally ignore errors
    int ok = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &ok, sizeof(ok));

    if( bind(s, res->ai_addr, res->ai_addrlen) == -1 ) {
        ::close(s);
        freeaddrinfo(res);
        throw_unix_error(errno, "bind");
    }
    freeaddrinfo(res);

    if( listen(s, 128) == -1 ) {
        ::close(s);
        throw_unix_error(errno, "listen");
    }
    return s;
}

bool tcp_server_socket::on_readable() {
    while( true ) {
        union {
            struct sockaddr sa;
            struct sockaddr_storage ss;
        } addr;
        socklen_t addrlen = sizeof(addr);
#ifdef _GNU_SOURCE
        int s = ::accept4(m_fd, &(addr.sa), &addrlen,
                          SOCK_NONBLOCK|SOCK_CLOEXEC);
#else
        int s = ::accept(m_fd, &(addr.sa), &addrlen);
        if( s != -1 ) {
            int fl = fcntl(s, F_GETFL, 0);
            if( fl == -1 ) fl = 0;
            if( fcntl(s, F_SETFL, fl | O_NONBLOCK) == -1 ) {
                ::close(s);
                throw_unix_error(errno, "fcntl(F_SETFL)");
            }
            fl = fcntl(s, F_GETFD, 0);
            if( fl == -1 ) fl = 0;
            if( fcntl(s, F_SETFD, fl | FD_CLOEXEC) == -1 ) {
                ::close(s);
                throw_unix_error(errno, "fcntl(F_SETFD)");
            }
        }
#endif
        if( s == -1 ) {
            if( errno == EINTR || errno == ECONNABORTED )
                continue;
            if( errno == EMFILE || errno == ENFILE ) {
                logger::error() << "accept: Too many open files.";
                continue;
            }
            if( errno == EAGAIN || errno == EWOULDBLOCK )
                break;
            throw_unix_error(errno, "accept");
        }

        try {
            std::unique_ptr<tcp_socket> t = m_wrapper(s, ip_address(&(addr.sa)));
            if( t.get() == nullptr ) {
                ::close(s);
            } else {
                m_reactor->add_resource( std::move(t),
                                         reactor::EVENT_IN|reactor::EVENT_OUT );
            }

        } catch( ... ) {
            ::close(s);
            throw;
        }
    }
    return true;
}

} // namespace cybozu
