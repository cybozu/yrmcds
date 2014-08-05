// (C) 2014 Cybozu.

#include "handler.hpp"

#include "../config.hpp"
#include "stats.hpp"
#include "sockets.hpp"

#include <cybozu/logger.hpp>

namespace {

const enum std::memory_order relaxed = std::memory_order_relaxed;

}

namespace yrmcds { namespace counter {

handler::handler(const std::function<cybozu::worker*()>& finder,
                 cybozu::reactor& reactor)
    : m_finder(finder),
      m_reactor(reactor),
      m_hash(g_config.counter().buckets()) {
}

bool handler::gc_ready() {
    std::time_t now = g_current_time.load(relaxed);

    if( m_gc_thread.get() != nullptr ) {
        if( ! m_gc_thread->done() )
            return false;
        m_last_gc = now;
        m_gc_thread = nullptr;  // join
    }

    unsigned int interval = g_config.counter().consumption_stats_interval();
    std::time_t boundary = (now / interval) * interval;
    return m_last_gc < boundary;
}

void handler::on_master_start() {
    cybozu::tcp_server_socket::wrapper w =
        [this](int s, const cybozu::ip_address&) {
            return make_counter_socket(s);
        };
    std::unique_ptr<cybozu::tcp_server_socket> ss =
        cybozu::make_server_socket(nullptr, g_config.counter().port(), w);
    m_reactor.add_resource(std::move(ss), cybozu::reactor::EVENT_IN);
}

std::unique_ptr<cybozu::tcp_socket> handler::make_counter_socket(int s) {
    unsigned int mc = g_config.counter().max_connections();
    if( mc != 0 &&
        g_stats.curr_connections.load(relaxed) >= mc )
        return nullptr;

    return std::unique_ptr<cybozu::tcp_socket>(
        new counter_socket(s, m_finder, m_hash) );
}

void handler::on_master_interval() {
    if( gc_ready() ) {
        m_gc_thread = std::unique_ptr<gc_thread>(new gc_thread(m_hash));
        m_gc_thread->start();
    }
}

void handler::on_master_end() {
    m_gc_thread = nullptr; // join
}

void handler::dump_stats() {
    std::uint64_t ops = 0;
    for( auto& v: g_stats.ops ) {
        ops += v.load(relaxed);
    }
    using logger = cybozu::logger;
    logger::info() << "counter: "
                   << g_stats.objects.load(relaxed) << " objects, "
                   << g_stats.curr_connections.load(relaxed) << " clients, "
                   << ops << " total ops.";
}

void handler::clear() {
    for( auto& bucket: m_hash )
        bucket.clear_nolock();
    g_stats.total_objects.store(0, relaxed);
}

}} // namespace yrmcds::counter
