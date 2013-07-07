// (C) 2013 Cybozu.

#include "replication.hpp"
#include "sockets.hpp"
#include "worker.hpp"

#include <cybozu/util.hpp>

#include <cstddef>
#include <cstring>
#include <sys/socket.h>
#include <sys/types.h>

namespace yrmcds {

bool memcache_socket::on_readable() {
    if( m_busy.load(std::memory_order_acquire) ) {
        m_reactor->add_readable(*this);
        return true;
    }

    // find an idle worker.
    worker* w = m_finder();
    if( w == nullptr ) {
        m_reactor->add_readable(*this);
        return true;
    }

    cybozu::dynbuf& buf = w->get_buffer();
    buf.reset();
    if( ! m_pending.empty() ) {
        buf.append(m_pending.data(), m_pending.size());
        m_pending.reset();
    }

    std::size_t to_receive = MAX_RECVSIZE;
    char* p = buf.prepare(MAX_RECVSIZE);
    while( to_receive > 0 ) {
        ssize_t n = ::recv(m_fd, p, to_receive, 0);
        if( n == -1 ) {
            if( errno == EAGAIN || errno == EWOULDBLOCK )
                break;
            if( errno == EINTR )
                continue;
            cybozu::throw_unix_error(errno, "recv");
        }
        if( n == 0 )
            return invalidate();
        to_receive -= n;
        p += n;
    }
    if( to_receive == 0 )
        m_reactor->add_readable(*this);
    if( to_receive == MAX_RECVSIZE ) {
        // unlikely
        m_reactor->add_readable(*this);
        return true;
    }
    buf.consume(MAX_RECVSIZE - to_receive);

    m_busy.store(true, std::memory_order_relaxed);
    w->post_job(this, [this](const char* p, std::size_t len) ->void {
            if( len > 0 )
                m_pending.append(p, len);
            m_busy.store(false, std::memory_order_release);
        });
    return true;
}

bool repl_socket::on_readable() {
    // recv and drop.
    while( true ) {
        ssize_t n = ::recv(m_fd, &m_recvbuf[0], MAX_RECVSIZE, 0);
        if( n == -1 ) {
            if( errno == EAGAIN || errno == EWOULDBLOCK )
                break;
            if( errno == EINTR )
                continue;
            cybozu::throw_unix_error(errno, "recv");
        }
        if( n == 0 )
            return invalidate();
    }
    return true;
}

bool repl_client_socket::on_readable() {
    while( true ) {
        char* p = m_recvbuf.prepare(MAX_RECVSIZE);
        ssize_t n = ::recv(m_fd, p, MAX_RECVSIZE, 0);
        if( n == -1 ) {
            if( errno == EAGAIN || errno == EWOULDBLOCK )
                break;
            if( errno == EINTR )
                continue;
            cybozu::throw_unix_error(errno, "recv");
        }
        if( n == 0 ) {
            m_reactor->quit();
            return invalidate();
        }
        m_recvbuf.consume(n);

        std::size_t c = repl_recv(m_recvbuf.data(), m_recvbuf.size(), m_hash);
        m_recvbuf.erase(c);
    }
    return true;
}

} // namespace yrmcds
