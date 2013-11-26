// Logics and data structures for the main thread.
// (C) 2013 Cybozu.

#ifndef YRMCDS_SERVER_HPP
#define YRMCDS_SERVER_HPP

#include "config.hpp"
#include "gc.hpp"
#include "object.hpp"
#include "sync.hpp"

#include <cybozu/hash_map.hpp>
#include <cybozu/ip_address.hpp>
#include <cybozu/reactor.hpp>
#include <cybozu/worker.hpp>

#include <ctime>
#include <functional>
#include <vector>

namespace yrmcds {

// The yrmcds server
class server {
public:
    server();

    static bool is_master() {
        return cybozu::has_ip_address( g_config.vip() );
    }

    void serve();

private:
    bool m_is_slave;
    bool m_signaled = false;
    cybozu::hash_map<object> m_hash;
    cybozu::reactor m_reactor;
    std::vector<std::unique_ptr<cybozu::worker>> m_workers;
    int m_worker_index = 0;
    std::time_t m_last_gc = 0;
    std::unique_ptr<gc_thread> m_gc_thread = nullptr;
    int m_consecutive_gcs = 0;
    std::vector<cybozu::tcp_socket*> m_slaves;
    std::vector<cybozu::tcp_socket*> m_new_slaves;
    syncer m_syncer;
    std::function<cybozu::worker*()> m_finder;

    bool gc_ready();
    bool reactor_gc_ready();
    void clear_everything();
    void serve_slave();
    void serve_master();

    std::unique_ptr<cybozu::tcp_socket> make_memcache_socket(int s);
    std::unique_ptr<cybozu::tcp_socket> make_repl_socket(int s);
};

} // namespace yrmcds

#endif // YRMCDS_SERVER_HPP
