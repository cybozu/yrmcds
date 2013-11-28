// (C) 2013 Cybozu.

#include "constants.hpp"
#include "server.hpp"
#include "sockets.hpp"
#include "stats.hpp"

#include <cybozu/logger.hpp>
#include <cybozu/signal.hpp>
#include <cybozu/tcp.hpp>

#include <signal.h>
#include <thread>

namespace yrmcds {

server::server():
    m_is_slave( ! is_master() ),
    m_hash(g_config.buckets()), m_syncer(m_workers) {
    m_slaves.reserve(MAX_SLAVES);
    m_new_slaves.reserve(MAX_SLAVES);
    m_finder = [this]() ->cybozu::worker* {
        std::size_t n_workers = m_workers.size();
        for( std::size_t i = 0; i < n_workers; ++i ) {
            cybozu::worker* pw = m_workers[m_worker_index].get();
            m_worker_index = (m_worker_index + 1) % n_workers;
            if( ! pw->is_running() )
                return pw;
        }
        return nullptr;
    };
}

inline bool server::gc_ready() {
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

inline bool server::reactor_gc_ready() {
    if( ! m_reactor.has_garbage() ) return false;
    if( ! m_syncer.empty() ) return false;
    if( m_gc_thread.get() != nullptr ) return false;
    if( ! m_new_slaves.empty() ) return false;
    return true;
}

inline void server::clear_everything() {
    for( auto& bucket: m_hash )
        bucket.clear_nolock();
    g_stats.total_objects.store(0, std::memory_order_relaxed);
}

void server::serve() {
    auto res = cybozu::signal_setup({SIGHUP, SIGQUIT, SIGTERM, SIGINT});
    res->set_handler([this](const struct signalfd_siginfo& si,
                            cybozu::reactor& r) {
                         switch(si.ssi_signo) {
                         case SIGHUP:
                             cybozu::logger::instance().reopen();
                             cybozu::logger::info() << "got SIGHUP.";
                             break;
                         case SIGQUIT:
                         case SIGTERM:
                         case SIGINT:
                             m_signaled = true;
                             r.quit();
                             break;
                         }
                     });
    m_reactor.add_resource(std::move(res), cybozu::reactor::EVENT_IN);

    using cybozu::make_server_socket;
    cybozu::tcp_server_socket::wrapper w =
        [this](int s, const cybozu::ip_address&) {
        return make_memcache_socket(s); };
    m_reactor.add_resource(make_server_socket(NULL, g_config.port(), w),
                           cybozu::reactor::EVENT_IN);

    while( m_is_slave ) {
        clear_everything();
        serve_slave();
        if( m_signaled ) return;

        // disconnected from the master
        for( int i = 0; i < MASTER_CHECKS; ++i ) {
            if( is_master() ) {
                m_is_slave = false;
                break;
            }
            std::this_thread::sleep_for( std::chrono::milliseconds(100) );
        }
    }

    cybozu::tcp_server_socket::wrapper w2 =
        [this](int s, const cybozu::ip_address&) {
        return make_repl_socket(s); };
    m_reactor.add_resource(make_server_socket(NULL, g_config.repl_port(), w2),
                           cybozu::reactor::EVENT_IN);

    serve_master();
}

void server::serve_slave() {
    int fd = cybozu::tcp_connect(g_config.vip().str().c_str(),
                                 g_config.repl_port());
    if( fd == -1 ) {
        m_reactor.run_once();
        return;
    }

    repl_client_socket* rs = new repl_client_socket(fd, m_hash);
    m_reactor.add_resource(std::unique_ptr<cybozu::resource>(rs),
                           cybozu::reactor::EVENT_IN|cybozu::reactor::EVENT_OUT );

    cybozu::logger::info() << "Slave start";
    m_reactor.run([rs](cybozu::reactor& r) {
            std::time_t now = std::time(nullptr);
            g_stats.current_time.store(now, std::memory_order_relaxed);

            if( is_master() ) {
                if( rs->valid() )
                    r.remove_resource(*rs);
                r.quit();
                return;
            }

            // ping to the master
            char c = '\0';
            rs->send(&c, sizeof(c), true);

            r.fix_garbage();
            r.gc();
        });

    cybozu::logger::info() << "Slave end";
}

void server::serve_master() {
    cybozu::logger::info() << "Entering master mode";

    auto callback = [this](cybozu::reactor& r) {
        std::time_t now = std::time(nullptr);
        g_stats.current_time.store(now, std::memory_order_relaxed);

        for( auto it = m_slaves.begin(); it != m_slaves.end(); ) {
            if( ! (*it)->valid() ) {
                it = m_slaves.erase(it);
            } else {
                ++it;
            }
        }

        if( ! m_syncer.empty() )
            m_syncer.check();

        if( gc_ready() ) {
            m_gc_thread = std::unique_ptr<gc_thread>(
                new gc_thread(m_hash, m_slaves, m_new_slaves));
            m_new_slaves.clear();
            m_gc_thread->start();
        }

        if( reactor_gc_ready() ) {
            m_reactor.fix_garbage();
            m_syncer.add_request(
                std::unique_ptr<sync_request>(
                    new sync_request([this]{ m_reactor.gc(); })
                    ));
        }
    };

    for( unsigned int i = 0; i < g_config.workers(); ++i )
        m_workers.emplace_back(new cybozu::worker(WORKER_BUFSIZE));
    for( auto& w: m_workers )
        w->start();

    auto stop = [this] {
        m_reactor.invalidate();
        for( auto& w: m_workers )
            w->stop();
        if( m_gc_thread.get() != nullptr )
            m_gc_thread = nullptr; // join
    };

    try {
        cybozu::logger::info() << "Reactor thread id="
                               << std::this_thread::get_id();
        m_reactor.run(callback);
        cybozu::logger::info() << "Exiting";

    } catch( ... ) {
        stop();
        throw;
    }

    stop();
}

std::unique_ptr<cybozu::tcp_socket> server::make_memcache_socket(int s) {
    if( m_is_slave )
        return nullptr;

    unsigned int mc = g_config.max_connections();
    if( mc != 0 &&
        (g_stats.curr_connections.load(std::memory_order_relaxed) >= mc) )
        return nullptr;

    return std::unique_ptr<cybozu::tcp_socket>(
        new memcache_socket(s, m_finder, m_hash, m_slaves) );
}

std::unique_ptr<cybozu::tcp_socket> server::make_repl_socket(int s) {
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

} // namespace yrmcds
