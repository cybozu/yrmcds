// Asynchronous socket abstraction.
// (C) 2013 Cybozu.

#ifndef CYBOZU_TCP_HPP
#define CYBOZU_TCP_HPP

#include "dynbuf.hpp"
#include "ip_address.hpp"
#include "reactor.hpp"
#include "util.hpp"

#include <cerrno>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <tuple>
#include <unistd.h>
#include <vector>

namespace cybozu {

// Create a TCP socket and connect to `node:port`.
// @node    The node name or IP address to connect.
// @port    The TCP port number to connect.
// @timeout Seconds before timeout.
//
// This creates a TCP socket and connect to a remote node.
// The returned socket is set non-blocking.  If `timeout` is 0,
// this will wait until the kernel gives up.  If `node` is `NULL`,
// this will try to connect to the local loopback interface.
//
// This function may return `-1` when it cannot establish a connection
// within `timeout` seconds or when the peer node denies connection.
// For other (serious) errors, exceptions will be thrown.
//
// @return  A valid UNIX file descriptor, or `-1` for non-fatal errors.
int tcp_connect(const char* node, std::uint16_t port, unsigned int timeout=10);


// A <cybozu::resource> subclass for connected TCP sockets.
//
// This is an abstract base class implementing <resource::on_writable>
// and provides <send>, <sendv>, <send_close>, and <sendv_close> member
// functions for connected TCP sockets.
//
// Derived classes still need to implement <resource::on_readable>.
class tcp_socket: public resource {
    static const std::size_t  SENDBUF_SIZE = 1 << 20;

public:
    // Construct an already connected socket.
    // @fd      A file descriptor of a connected socket.
    // @bufcnt  The number of 1 MiB buffers for pending send data.
    //
    // Construct a socket resource with a connected socket file descriptor.
    // The socket should already be set non-blocking.
    explicit tcp_socket(int fd, unsigned int bufcnt = 0);
    virtual ~tcp_socket() {
        free_buffers();
    }

    // The maximum size of <iovec> array for <sendv>.
    static const int MAX_IOVCNT = 20;

    // struct for <sendv> and <sendv_close>.
    struct iovec {
        const char* p;
        std::size_t len;
    };

    // Atomically send data.
    // @p     Data to be sent.
    // @len   Length of data starting from `p`.
    // @flush If `true`, the kernel send buffer will be flushed.
    //
    // This function sends a chunk of data atomically.  The reactor
    // thread should not call this, or it may be blocked forever.
    //
    // @return `true` if this socket is valid, `false` otherwise.
    bool send(const char* p, std::size_t len, bool flush=false) {
        return with_fd([=](int fd) -> bool {
            lock_guard g(m_lock);
            if( ! _send(fd, p, len, g) )
                return false;
            if( flush && empty() )
                _flush(fd);
            return true;
        });
    }

    // Atomically send multiple data.
    // @iov     Array of <iovec>.
    // @iovcnt  Number of elements in `iov`.
    // @flush   If `true`, the kernel send buffer will be flushed.
    //
    // This function sends a chunk of data atomically.  The reactor
    // thread should not call this, or it may be blocked forever.
    //
    // @return `true` if this socket is valid, `false` otherwise.
    bool sendv(const iovec* iov, int iovcnt, bool flush=false) {
        return with_fd([=](int fd) -> bool {
            if( iovcnt >= MAX_IOVCNT )
                throw std::logic_error("<tcp_socket::sendv> too many iovec.");
            lock_guard g(m_lock);
            if( ! _sendv(fd, iov, iovcnt, g) )
                return false;
            if( flush && empty() )
                _flush(fd);
            return true;
        });
    }

    // Atomically send data, then close the socket.
    // @p     Data to be sent.
    // @len   Length of data starting from `p`.
    //
    // This function sends a chunk of data atomically.  The socket
    // will be closed after data are sent.  The reactor thread should
    // not call this, or it may be blocked forever.
    //
    // @return `true` if this socket is valid, `false` otherwise.
    bool send_close(const char* p, std::size_t len) {
        return with_fd([=](int fd) -> bool {
            lock_guard g(m_lock);
            if( ! _send(fd, p, len, g) )
                return false;
            m_shutdown = true;
            if( empty() ) {
                _flush(fd);
                return false;
            }
            g.unlock();
            m_cond_write.notify_all();
            return true;
        });
    }

    // Atomically send multiple data, then close the socket.
    // @iov     Array of <iovec>.
    // @iovcnt  Number of elements in `iov`.
    //
    // This function sends a chunk of data atomically.  The socket
    // will be closed after data are sent.  The reactor thread should
    // not call this, or it may be blocked forever.
    //
    // @return `true` if this socket is valid, `false` otherwise.
    bool sendv_close(const iovec* iov, int iovcnt) {
        return with_fd([=](int fd) -> bool {
            if( iovcnt >= MAX_IOVCNT )
                throw std::logic_error("<tcp_socket::sendv> too many iov.");
            lock_guard g(m_lock);
            if( ! _sendv(fd, iov, iovcnt, g) )
                return false;
            m_shutdown = true;
            if( empty() ) {
                _flush(fd);
                return false;
            }
            g.unlock();
            m_cond_write.notify_all();
            return true;
        });
    }

    enum class recv_result {
        OK,    // Received some data.
        AGAIN, // No data was available.
        RESET, // Connection reset by peer.
        NONE,  // Peer half-closed the connection.
    };

    // Receive data from the socket.
    // @buf           A buffer to store received data.
    // @max_recvsize  Maximum size of data to be received.
    //
    // This function receives data from the socket.  Since this uses `with_fd`
    // internally, the reactor thread should not call this, or it may be blocked.
    //
    // @return The result of the operation.
    recv_result recv(dynbuf& buf, const std::size_t max_recvsize) {
        char* p = buf.prepare(max_recvsize);
        ::ssize_t n;
        auto ret = with_fd([=, &n](int fd) -> bool {
            do {
                n = ::recv(fd, p, max_recvsize, 0);
            } while( n == -1 && errno == EINTR );

            return (n != -1) || (errno != ECONNRESET);
        });

        if( ! ret ) {
            return recv_result::RESET;
        }

        if( n == 0 ) {
            return recv_result::NONE;
        }

        if( n == -1 ) {
            if( errno == EAGAIN || errno == EWOULDBLOCK )
                return recv_result::AGAIN;
            throw_unix_error(errno, "recv");
        }

        buf.consume(n);
        return recv_result::OK;
    }

protected:
    // Write out pending data.
    //
    // This method tries to send pending data as much as possible.
    //
    // @return `false` if some error happened, `true` otherwise.
    bool write_pending_data(int fd);

    // Just call <write_pending_data>.
    //
    // The default implementation just invoke <write_pending_data>.
    // You may override this to dispatch the job to another thread.
    virtual bool on_writable(int fd) override {
        if( write_pending_data(fd) )
            return true;
        return invalidate();
    }

    virtual void on_invalidate(int fd) override {
        ::shutdown(fd, SHUT_RDWR);
        free_buffers();
        m_cond_write.notify_all();
    }

    // This method will be called everytime when <send>, <sendv>,
    // <send_close>, or <sendv_close> is blocked because of internal buffer full.
    //
    // Subclasses can override this to handle the buffer full event.
    virtual void on_buffer_full() {}

private:
    std::vector<char*> m_free_buffers;
    // tuple of <pointer, data written, data sent>
    std::vector<std::tuple<char*, std::size_t, std::size_t>> m_pending;
    std::vector<char> m_tmpbuf;
    bool m_shutdown = false;
    typedef std::unique_lock<std::mutex> lock_guard;
    mutable std::mutex m_lock;
    mutable std::condition_variable m_cond_write;

    std::size_t capacity() const {
        std::size_t c = m_free_buffers.size() * SENDBUF_SIZE;
        if( m_pending.empty() ) return c;
        return c + SENDBUF_SIZE - std::get<1>(m_pending.back());
    }
    bool can_send(std::size_t len) const {
        if( m_shutdown ) return true; // in fact, fail
        if( ! m_tmpbuf.empty() ) return false;
        if( m_pending.empty() ) return true;
        return capacity() >= len;
    }
    bool _send(int fd, const char* p, std::size_t len, lock_guard& g);
    bool _sendv(int fd, const iovec* iov, const int iovcnt, lock_guard& g);
    bool empty() const {
        return m_pending.empty() && m_tmpbuf.empty();
    }
    void _flush(int fd) {
        // with TCP_CORK, setting TCP_NODELAY effectively flushes
        // the kernel send buffer.
        int v = 1;
        if( setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &v, sizeof(v)) == -1 )
            throw_unix_error(errno, "setsockopt(TCP_NODELAY)");
    }
    void free_buffers();
};


// A helper function to create a server socket.
// @bind_addr  A numeric IP address to be bound, or `NULL`.
// @port       TCP port number to be bound.
// @freebind   true to turn on IP_FREEBIND socket option.
//
// This is a helper function for <tcp_server_socket> template.
// The socket is set non-blocking before return.
//
// @return  A UNIX file descriptor of a listening socket.
int
setup_server_socket(const char* bind_addr, std::uint16_t port, bool freebind);


// A <cybozu::resource> subclass to accept new TCP connections.
class tcp_server_socket: public resource {
public:
    using wrapper = std::function<std::unique_ptr<tcp_socket>
                                  (int s, const ip_address&)>;

    // Construct a server socket.
    // @bind_addr  A numeric IP address to be bound, or `NULL`.
    // @port       TCP port number to be bound.
    // @on_accept  Callback function.
    // @freebind   true to turn on IP_FREEBIND socket option.
    //
    // This creates a socket and bind it to the given address and port.
    // If `bind_addr` is `NULL`, the socket will listen on any address.
    // Both IPv4 and IPv6 addresses are supported.
    //
    // For each new connection, `on_accept` is called to determine
    // if the new connection need to be closed immediately or
    // to be added to the reactor.  If `on_accept` returns an empty
    // <std::unique_ptr>, the new connection is closed immediately.
    // Otherwise, the new connection is added to the reactor.
    tcp_server_socket(const char* bind_addr, std::uint16_t port,
                      wrapper on_accept, bool freebind):
        resource( setup_server_socket(bind_addr, port, freebind) ),
        m_wrapper(on_accept) {}
    virtual ~tcp_server_socket() {}

private:
    wrapper m_wrapper;

    virtual bool on_readable(int) override final;
    virtual bool on_writable(int) override final { return true; }
};


// Utility function to create a <std::unique_ptr> of <tcp_server_socket>.
inline std::unique_ptr<tcp_server_socket> make_server_socket(
    const char* bind_addr, std::uint16_t port,
    tcp_server_socket::wrapper w, bool freebind = false) {
    return std::unique_ptr<tcp_server_socket>(
        new tcp_server_socket(bind_addr, port, w, freebind) );
}
inline std::unique_ptr<tcp_server_socket> make_server_socket(
    const ip_address& ip, std::uint16_t port,
    tcp_server_socket::wrapper w, bool freebind = false) {
    return make_server_socket(ip.str().c_str(), port, w, freebind);
}

} // namespace cybozu

#endif // CYBOZU_SOCKET_HPP
