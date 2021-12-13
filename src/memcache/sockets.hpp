// Defines sockets for yrmcds.
// (C) 2013 Cybozu.

#ifndef YRMCDS_MEMCACHE_SOCKETS_HPP
#define YRMCDS_MEMCACHE_SOCKETS_HPP

#include "../constants.hpp"
#include "memcache.hpp"
#include "object.hpp"
#include "stats.hpp"

#include <cybozu/dynbuf.hpp>
#include <cybozu/hash_map.hpp>
#include <cybozu/tcp.hpp>
#include <cybozu/util.hpp>
#include <cybozu/worker.hpp>

#include <functional>
#include <vector>

namespace yrmcds { namespace memcache {

class repl_socket;

class memcache_socket: public cybozu::tcp_socket {
public:
    memcache_socket(int fd,
                    const std::function<cybozu::worker*()>& finder,
                    cybozu::hash_map<object>& hash,
                    const std::vector<repl_socket*>& slaves);
    virtual ~memcache_socket();

    void add_lock(const cybozu::hash_key& k) {
        // m_locks are accessed by at most one worker thread, hence
        // there is no need to guard it with a mutex.
        m_locks.emplace_back(k);
    }

    void remove_lock(const cybozu::hash_key& k) {
        for( auto it = m_locks.begin(); it != m_locks.end(); ++it ) {
            if( it->get() == k ) {
                m_locks.erase(it);
                return;
            }
        }
        cybozu::dump_stack();
        throw std::logic_error("<memcache_socket::remove_lock> bug");
    }

    void unlock_all() {
        for( auto& ref: m_locks ) {
            m_hash.apply(ref.get(),
                         [](const cybozu::hash_key&, object& obj) -> bool {
                             obj.unlock(false);
                             return true;
                         }, nullptr);
        }
        m_locks.clear();
    }

    // Process a binary request command.
    // @cmd     A binary request.
    void cmd_bin(const memcache::binary_request& cmd);

    // Process a test request command.
    // @cmd     A binary request.
    void cmd_text(const memcache::text_request& cmd);

private:
    alignas(CACHELINE_SIZE)
    std::atomic<bool> m_busy;
    const std::function<cybozu::worker*()>& m_finder;
    cybozu::hash_map<object>& m_hash;
    cybozu::dynbuf m_pending;
    const std::vector<repl_socket*>& m_slaves_origin;
    std::vector<repl_socket*> m_slaves;
    cybozu::worker::job m_recvjob;
    cybozu::worker::job m_sendjob;
    std::vector<std::reference_wrapper<const cybozu::hash_key>> m_locks;

    virtual void on_invalidate() override final {
        // In order to avoid races and deadlocks, remaining locks
        // are not released here.  They are released in the destructor
        // where no other threads have access to this object.
        g_stats.curr_connections.fetch_sub(1);
        cybozu::tcp_socket::on_invalidate();
    }
    virtual bool on_readable() override final;
    virtual bool on_writable() override final;
};


class object;

class repl_socket: public cybozu::tcp_socket {
public:
    repl_socket(int fd, unsigned int bufcnt,
                const std::function<cybozu::worker*()>& finder)
        : cybozu::tcp_socket(fd, bufcnt),
          m_finder(finder),
          m_recvbuf(MAX_RECVSIZE),
          m_last_heartbeat(g_current_time.load(std::memory_order_relaxed))
    {
        m_sendjob = [this](cybozu::dynbuf&) {
            if( ! write_pending_data() )
                invalidate_and_close();
        };
    }

    bool timed_out() const {
        std::time_t now = g_current_time.load(std::memory_order_relaxed);
        return m_last_heartbeat + g_config.slave_timeout() <= now;
    }

    virtual void on_buffer_full() override {
        cybozu::logger::warning()
            << "Replication buffer is full. "
            << "Please enlarge \"repl_buffer_size\" to avoid the hang up of yrmcds.";
    }

private:
    const std::function<cybozu::worker*()>& m_finder;
    std::vector<char> m_recvbuf;
    cybozu::worker::job m_sendjob;
    std::time_t m_last_heartbeat;

    virtual bool on_readable() override final;
    virtual bool on_writable() override final;
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
        cybozu::logger::warning() << "The connection to master has hung up.";
        m_reactor->quit();
        return invalidate();
    }
    virtual bool on_error() override final {
        cybozu::logger::warning() << "An error occurred on the connection to master.";
        m_reactor->quit();
        return invalidate();
    }
};

}} // namespace yrmcds::memcache

#endif // YRMCDS_MEMCACHE_SOCKETS_HPP
