// (C) 2013-2014 Cybozu.

#include "constants.hpp"
#include "memcache/handler.hpp"
#include "counter/handler.hpp"
#include "server.hpp"
#include "global.hpp"

#include <cybozu/logger.hpp>
#include <cybozu/signal.hpp>
#include <cybozu/tcp.hpp>

#include <algorithm>
#include <cstdlib>
#include <signal.h>
#include <thread>

namespace yrmcds {

server::server(): m_syncer(m_workers) {
    auto finder = [this]() ->cybozu::worker* {
        std::size_t n_workers = m_workers.size();
        for( std::size_t i = 0; i < n_workers; ++i ) {
            cybozu::worker* pw = m_workers[m_worker_index].get();
            m_worker_index = (m_worker_index + 1) % n_workers;
            if( ! pw->is_running() )
                return pw;
        }
        return nullptr;
    };

    m_handlers.emplace_back(new memcache::handler(finder, m_reactor, m_syncer));
    if( g_config.counter().enable() )
        m_handlers.emplace_back(new counter::handler(finder, m_reactor));
}

inline bool server::reactor_gc_ready() {
    if( ! m_reactor.has_garbage() ) return false;
    if( ! m_syncer.empty() ) return false;
    auto ready = [](const std::unique_ptr<protocol_handler>& p) {
        return p->reactor_gc_ready();
    };
    return std::all_of(m_handlers.cbegin(), m_handlers.cend(), ready);
}

void server::serve() {
    auto res = cybozu::signal_setup({SIGHUP, SIGQUIT, SIGTERM, SIGINT, SIGUSR1});
    res->set_handler([this](const struct signalfd_siginfo& si,
                            cybozu::reactor& r) {
                         switch(si.ssi_signo) {
                         case SIGUSR1:
                             for( auto& handler: m_handlers )
                                 handler->dump_stats();
                             break;
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

    for( auto& handler: m_handlers )
        handler->on_start();

    if( is_master() )
        goto MASTER_ENTRY;
    while( true ) {
        for( auto& handler: m_handlers )
            handler->clear();

        serve_slave();
        if( m_signaled ) return;

        // disconnected from the master
        for( int i = 0; i < MASTER_CHECKS; ++i ) {
            if( is_master() )
                goto MASTER_ENTRY;
            std::this_thread::sleep_for( std::chrono::milliseconds(100) );
        }
    }

  MASTER_ENTRY:
    serve_master();
    std::quick_exit(0);
}

void server::serve_slave() {
    for( auto it1 = m_handlers.begin(); it1 != m_handlers.end(); ++it1 ) {
        if( ! (*it1)->on_slave_start() ) {
            // failed to start. stop already started handlers.
            for( auto it2 = m_handlers.begin(); it2 != it1; ++it2 )
                (*it2)->on_slave_end();
            return;
        }
    }

    cybozu::logger::info() << "Slave start";

    m_reactor.run([this](cybozu::reactor& r) {
            if( is_master() ) {
                r.quit();
                return;
            }

            std::time_t now = std::time(nullptr);
            g_current_time.store(now, std::memory_order_relaxed);

            for( auto& handler: m_handlers )
                handler->on_slave_interval();

            r.fix_garbage();
            r.gc();
        });
    for( auto& handler: m_handlers )
        handler->on_slave_end();

    cybozu::logger::info() << "Slave end";
}

void server::serve_master() {
    cybozu::logger::info() << "Entering master mode";

    for( auto& handler: m_handlers )
        handler->on_master_start();

    auto callback = [this](cybozu::reactor&) {
        std::time_t now = std::time(nullptr);
        g_current_time.store(now, std::memory_order_relaxed);

        if( ! m_syncer.empty() )
            m_syncer.check();
        for( auto& handler: m_handlers )
            handler->on_master_interval();

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
        for( auto& handler: m_handlers )
            handler->on_master_end();
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

} // namespace yrmcds
