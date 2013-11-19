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

    m_busy.store(true, std::memory_order_release);
    w->post_job(this);
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
            if( errno == ECONNRESET )
                return invalidate();
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
            if( errno == ECONNRESET ) {
                m_reactor->quit();
                return invalidate();
            }
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
