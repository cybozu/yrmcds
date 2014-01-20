// (C) 2014 Cybozu.

#include "handler.hpp"
#include "../constants.hpp"

namespace yrmcds { namespace memcache {

memcache_handler::memcache_handler(const std::function<cybozu::worker*()>& finder,
                                   const std::function<bool()>& is_slave,
                                   cybozu::reactor& reactor,
                                   syncer& sync):
    m_finder(finder),
    m_is_slave(is_slave),
    m_reactor(reactor),
    m_syncer(sync),
    m_hash(g_config.buckets()) {
    m_slaves.reserve(MAX_SLAVES);
    m_new_slaves.reserve(MAX_SLAVES);
}

bool memcache_handler::gc_ready() {
    std::time_t now = std::time(nullptr);

    if( m_gc_thread.get() != nullptr ) {
        if( ! m_gc_thread->done() )
            return false;
        m_last_gc = now;
        m_gc_thread = nullptr;
    }

    std::time_t t = g_stats.flush_time.load(std::memory_order_relaxed);
    if( t != 0 && now >= t )
        return true;

    // run GC immediately if the heap is over used.
    if( g_stats.used_memory.load(std::memory_order_relaxed) >
        g_config.memory_limit() ) return true;

    // Run GC when there are new slaves.
    // In case there are unstable slaves that try to connect to
    // the master too frequently, the number of consecutive GCs is limited.
    if( ! m_new_slaves.empty() && m_consecutive_gcs < MAX_CONSECUTIVE_GCS ) {
        ++m_consecutive_gcs;
        return true;
    }

    if( now > (m_last_gc + g_config.gc_interval()) ) {
        m_consecutive_gcs = 0;
        return true;
    }

    return false;
}

bool memcache_handler::reactor_gc_ready() {
    return m_gc_thread.get() == nullptr && m_new_slaves.empty();
}

void memcache_handler::on_start() {
    using cybozu::make_server_socket;
    cybozu::tcp_server_socket::wrapper w =
        [this](int s, const cybozu::ip_address&) {
        return make_memcache_socket(s); };
    m_reactor.add_resource(make_server_socket(NULL, g_config.port(), w),
                           cybozu::reactor::EVENT_IN);
}

void memcache_handler::on_master_start() {
    cybozu::tcp_server_socket::wrapper w =
        [this](int s, const cybozu::ip_address&) {
        return make_repl_socket(s); };
    m_reactor.add_resource(make_server_socket(NULL, g_config.repl_port(), w),
                           cybozu::reactor::EVENT_IN);
}

void memcache_handler::on_master_pre_sync() {
    std::time_t now = std::time(nullptr);
    g_stats.current_time.store(now, std::memory_order_relaxed);

    for( auto it = m_slaves.begin(); it != m_slaves.end(); ) {
        if( ! (*it)->valid() ) {
            it = m_slaves.erase(it);
        } else {
            ++it;
        }
    }
}

void memcache_handler::on_master_interval() {
    if( gc_ready() ) {
        m_gc_thread = std::unique_ptr<gc_thread>(
            new gc_thread(m_hash, m_slaves, m_new_slaves));
        m_new_slaves.clear();
        m_gc_thread->start();
    }
}

void memcache_handler::on_master_end() {
    if( m_gc_thread.get() != nullptr )
        m_gc_thread = nullptr; // join
}

void memcache_handler::on_slave_start(int fd) {
    m_repl_client_socket = new repl_client_socket(fd, m_hash);
    m_reactor.add_resource(std::unique_ptr<cybozu::resource>(m_repl_client_socket),
                           cybozu::reactor::EVENT_IN|cybozu::reactor::EVENT_OUT );
}

void memcache_handler::on_clear() {
    for( auto& bucket: m_hash )
        bucket.clear_nolock();
    g_stats.total_objects.store(0, std::memory_order_relaxed);
}

void memcache_handler::on_slave_interval() {
    std::time_t now = std::time(nullptr);
    g_stats.current_time.store(now, std::memory_order_relaxed);

    // ping to the master
    char c = '\0';
    m_repl_client_socket->send(&c, sizeof(c), true);
}

void memcache_handler::on_slave_end() {
    if( m_repl_client_socket->valid() )
        m_reactor.remove_resource(*m_repl_client_socket);
    delete m_repl_client_socket;
    m_repl_client_socket = nullptr;
}

std::unique_ptr<cybozu::tcp_socket> memcache_handler::make_memcache_socket(int s) {
    if( m_is_slave() )
        return nullptr;

    unsigned int mc = g_config.max_connections();
    if( mc != 0 &&
        (g_stats.curr_connections.load(std::memory_order_relaxed) >= mc) )
        return nullptr;

    return std::unique_ptr<cybozu::tcp_socket>(
        new memcache_socket(s, m_finder, m_hash, m_slaves) );
}

std::unique_ptr<cybozu::tcp_socket> memcache_handler::make_repl_socket(int s) {
    if( m_slaves.size() == MAX_SLAVES )
        return nullptr;
    std::unique_ptr<cybozu::tcp_socket> t( new repl_socket(s, m_finder) );
    cybozu::tcp_socket* pt = t.get();
    m_slaves.push_back(pt);
    m_syncer.add_request(
        std::unique_ptr<sync_request>(
            new sync_request([this,pt]{ m_new_slaves.push_back(pt); })
            ));
    return std::move(t);
}

}} // namespace yrmcds::memcache
