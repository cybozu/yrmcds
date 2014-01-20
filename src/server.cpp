// (C) 2013 Cybozu.

#include "constants.hpp"
#include "server.hpp"
#include "memcache/handler.hpp"
#include "semaphore/handler.hpp"

#include <cybozu/logger.hpp>
#include <cybozu/signal.hpp>
#include <cybozu/tcp.hpp>

#include <cstdlib>
#include <signal.h>
#include <thread>

namespace yrmcds {

server::server(): m_is_slave( ! is_master() ), m_syncer(m_workers) {
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
    auto is_slave = [this]{ return m_is_slave; };

    m_handlers.emplace_back(
        new memcache::memcache_handler(finder, is_slave, m_reactor, m_syncer));
    if( g_config.semaphore().enable() ) {
        m_handlers.emplace_back(
            new semaphore::semaphore_handler(finder, is_slave, m_reactor));
    }
}

inline bool server::reactor_gc_ready() {
    auto ready = [](const std::unique_ptr<protocol_handler>& p) {
        return p->reactor_gc_ready();
    };
    return m_reactor.has_garbage() && m_syncer.empty() &&
           std::all_of(m_handlers.begin(), m_handlers.end(), ready);
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

    for( auto& handler: m_handlers )
        handler->on_start();

    while( m_is_slave ) {
        for( auto& handler: m_handlers )
            handler->on_clear();

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

    serve_master();
    std::quick_exit(0);
}

void server::serve_slave() {
    int fd = cybozu::tcp_connect(g_config.vip().str().c_str(),
                                 g_config.repl_port());
    if( fd == -1 ) {
        m_reactor.run_once();
        return;
    }

    cybozu::logger::info() << "Slave start";

    for( auto& handler: m_handlers )
        handler->on_slave_start(fd);

    m_reactor.run([this](cybozu::reactor& r) {
            if( is_master() ) {
                for( auto& handler: m_handlers )
                    handler->on_slave_end();
                r.quit();
                return;
            }

            for( auto& handler: m_handlers )
                handler->on_slave_interval();

            r.fix_garbage();
            r.gc();
        });


    cybozu::logger::info() << "Slave end";
}

void server::serve_master() {
    cybozu::logger::info() << "Entering master mode";

    for( auto& handler: m_handlers )
        handler->on_master_start();

    auto callback = [this](cybozu::reactor&) {
        for( auto& handler: m_handlers )
            handler->on_master_pre_sync();
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
