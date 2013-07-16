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
    memcache_socket(int fd, std::function<worker*()>& finder,
                    std::function<void(const cybozu::hash_key&, bool)>& unlocker):
        cybozu::tcp_socket(fd), m_busy(false),
        m_finder(finder), m_unlocker(unlocker), m_pending(0) {}

    void add_lock(const cybozu::hash_key& k) {
        m_locks.emplace_back(k);
    }

    void remove_lock(const cybozu::hash_key& k) {
        for( auto it = m_locks.begin(); it != m_locks.end(); ++it ) {
            if( *it == k ) {
                m_locks.erase(it);
                return;
            }
        }
        throw std::logic_error("<memcache_socket::remove_lock> bug");
    }

    void unlock_all() {
        for( auto& ref: m_locks )
            m_unlocker(ref.get(), false);
        m_locks.clear();
    }

private:
    alignas(CACHELINE_SIZE)
    std::atomic<bool> m_busy;
    std::function<worker*()>& m_finder;
    std::function<void(const cybozu::hash_key&, bool)>& m_unlocker;
    cybozu::dynbuf m_pending;
    std::vector<std::reference_wrapper<const cybozu::hash_key>> m_locks;

    virtual void on_invalidate() override final {
        for( auto& ref: m_locks )
            m_unlocker(ref.get(), true); // force unlock
        m_locks.clear();
        cybozu::tcp_socket::on_invalidate();
    }
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
