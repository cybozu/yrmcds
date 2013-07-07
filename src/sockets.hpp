// Defines sockets for yrmcds.
// (C) 2013 Cybozu.

#ifndef YRMCDS_SOCKETS_HPP
#define YRMCDS_SOCKETS_HPP

#include "constants.hpp"
#include "stats.hpp"

#include <cybozu/dynbuf.hpp>
#include <cybozu/hash_map.hpp>
#include <cybozu/tcp.hpp>

#include <functional>
#include <vector>

namespace yrmcds {

class worker;

class memcache_socket: public cybozu::tcp_socket {
public:
    memcache_socket(int fd, std::function<worker*()> finder):
        cybozu::tcp_socket(fd), m_busy(false),
        m_finder(finder), m_pending(0) {}

private:
    alignas(CACHELINE_SIZE)
    std::atomic<bool> m_busy;
    std::function<worker*()> m_finder;
    cybozu::dynbuf m_pending;

    virtual bool on_readable() override final;
};


class object;

class repl_socket: public cybozu::tcp_socket {
public:
    repl_socket(int fd):
        cybozu::tcp_socket(fd, 30), m_recvbuf(MAX_RECVSIZE) {}

private:
    std::vector<char> m_recvbuf;

    virtual bool on_readable() override final;
};


class repl_client_socket: public cybozu::tcp_socket {
public:
    repl_client_socket(int fd, cybozu::hash_map<object>& m):
        cybozu::tcp_socket(fd), m_hash(m), m_recvbuf(30 << 20) {}

private:
    cybozu::hash_map<object>& m_hash;
    cybozu::dynbuf m_recvbuf;

    virtual bool on_readable() override final;
    virtual bool on_hangup() override final {
        m_reactor->quit();
        return invalidate();
    }
    virtual bool on_error() override final {
        m_reactor->quit();
        return invalidate();
    }
};

} // namespace yrmcds

#endif // YRMCDS_SOCKETS_HPP
