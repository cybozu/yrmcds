// Memcache protocol logics and data structures
// (C) 2014 Cybozu.

#ifndef YRMCDS_MEMCACHE_HANDLER_HPP
#define YRMCDS_MEMCACHE_HANDLER_HPP

#include "../handler.hpp"
#include "../sync.hpp"
#include "gc.hpp"
#include "object.hpp"
#include "sockets.hpp"

#include <cybozu/reactor.hpp>
#include <cybozu/worker.hpp>

#include <ctime>
#include <functional>

namespace yrmcds { namespace memcache {

class handler: public protocol_handler {
public:
    handler(const std::function<cybozu::worker*()>& finder,
            cybozu::reactor& reactor,
            syncer& syncer);
    virtual void on_start() override;
    virtual void on_master_start() override;
    virtual void on_master_interval() override;
    virtual void on_master_end() override;
    virtual bool on_slave_start() override;
    virtual void on_slave_end() override;
    virtual void on_slave_interval() override;
    virtual void dump_stats() override;
    virtual void clear() override;
    virtual bool reactor_gc_ready() const override;

private:
    bool gc_ready(std::time_t now);
    std::unique_ptr<cybozu::tcp_socket> make_memcache_socket(int s);
    std::unique_ptr<cybozu::tcp_socket> make_repl_socket(int s);

    std::function<cybozu::worker*()> m_finder;
    cybozu::reactor& m_reactor;
    syncer& m_syncer;
    bool m_is_slave = true;
    cybozu::hash_map<object> m_hash;
    std::time_t m_last_gc = 0;
    std::unique_ptr<gc_thread> m_gc_thread = nullptr;
    int m_consecutive_gcs = 0;
    std::vector<cybozu::tcp_socket*> m_slaves;
    std::vector<cybozu::tcp_socket*> m_new_slaves;
    repl_client_socket* m_repl_client_socket = nullptr;
};

}} // namespace yrmcds::memcache

#endif // YRMCDS_MEMCACHE_HANDLER_HPP
