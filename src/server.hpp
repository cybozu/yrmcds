// Logics and data structures for the main thread.
// (C) 2013 Cybozu.

#ifndef YRMCDS_SERVER_HPP
#define YRMCDS_SERVER_HPP

#include "config.hpp"
#include "handler.hpp"
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
    cybozu::reactor m_reactor;
    std::vector<std::unique_ptr<cybozu::worker>> m_workers;
    int m_worker_index = 0;
    syncer m_syncer;
    std::vector<std::unique_ptr<protocol_handler>> m_handlers;

    bool reactor_gc_ready();
    void serve_slave();
    void serve_master();
};

} // namespace yrmcds

#endif // YRMCDS_SERVER_HPP
