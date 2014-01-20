// Memcache protocol logics and data structures
// (C) 2014 Cybozu.

#ifndef YRMCDS_MEMCACHE_HANDLER_HPP
#define YRMCDS_MEMCACHE_HANDLER_HPP

#include "../handler.hpp"
#include "../sync.hpp"
#include "gc.hpp"
#include "object.hpp"
#include "sockets.hpp"
#include <cybozu/worker.hpp>

#include <functional>

namespace yrmcds { namespace memcache {

class memcache_handler: public protocol_handler {
public:
    memcache_handler(const std::function<cybozu::worker*()>& m_finder,
                     const std::function<bool()>& is_slave,
                     cybozu::reactor& reactor,
                     syncer& syncer);
    virtual void on_start() override;
    virtual void on_master_start() override;
    virtual void on_master_pre_sync() override;
    virtual void on_master_interval() override;
    virtual void on_master_end() override;
    virtual void on_slave_start(int fd) override;
    virtual void on_slave_end() override;
    virtual void on_slave_interval() override;
    virtual void on_clear() override;
    virtual bool reactor_gc_ready() override;

private:
    bool gc_ready();
    std::unique_ptr<cybozu::tcp_socket> make_memcache_socket(int s);
    std::unique_ptr<cybozu::tcp_socket> make_repl_socket(int s);

    std::function<cybozu::worker*()> m_finder;
    std::function<bool()> m_is_slave;
    cybozu::reactor& m_reactor;
    syncer& m_syncer;
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
