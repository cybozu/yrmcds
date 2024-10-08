// (C) 2014 Cybozu.

#include "sockets.hpp"
#include "object.hpp"

#include <cybozu/util.hpp>

#include <cstddef>
#include <cstring>
#include <sys/socket.h>
#include <sys/types.h>

namespace yrmcds { namespace counter {

counter_socket::counter_socket(int fd,
                                   const std::function<cybozu::worker*()>& finder,
                                   cybozu::hash_map<object>& hash)
    : cybozu::tcp_socket(fd),
      m_busy(false),
      m_finder(finder),
      m_hash(hash),
      m_pending(0) {
    g_stats.curr_connections.fetch_add(1);
    g_stats.total_connections.fetch_add(1);

    m_recvjob = [this](cybozu::dynbuf& buf) {
        // load pending data
        if( ! m_pending.empty() ) {
            buf.append(m_pending.data(), m_pending.size());
            m_pending.reset();
        }

        while( true ) {
            auto res = receive(buf, MAX_RECVSIZE);
            if( res == recv_result::AGAIN )
                break;
            if( res == recv_result::RESET || res == recv_result::NONE ) {
                buf.reset();
                release_all();
                break;
            }

            const char* head = buf.data();
            std::size_t len = buf.size();
            while( len > 0 ) {
                counter::request parser(head, len);
                std::size_t c = parser.length();
                if( c == 0 ) break;
                head += c;
                len -= c;
                execute(parser);
            }
            if( len > MAX_REQUEST_LENGTH ) {
                cybozu::logger::warning() << "denied too large request of "
                                          << len << " bytes.";
                buf.reset();
                release_all();
                invalidate_and_close();
                break;
            }
            buf.erase(head - buf.data());
        }

        // recv returns EAGAIN, or some error happens.
        if( buf.size() > 0 )
            m_pending.append(buf.data(), buf.size());

        m_busy.store(false, std::memory_order_release);
    };

    m_sendjob = [this](cybozu::dynbuf& buf) {
        with_fd([=](int fd) -> bool {
            return write_pending_data(fd);
        });
    };
}

counter_socket::~counter_socket() {
    // the destructor is the safe place to release remaining resources
    release_all();
}

bool counter_socket::on_readable(int fd) {
    if( m_busy.load(std::memory_order_acquire) ) {
        m_reactor->add_readable(*this);
        return true;
    }

    // find an idle worker.
    cybozu::worker* w = m_finder();
    if( w == nullptr ) {
        m_reactor->add_readable(*this);
        return true;
    }

    m_busy.store(true, std::memory_order_release);
    w->post_job(m_recvjob);
    return true;
}

bool counter_socket::on_writable(int fd) {
    cybozu::worker* w = m_finder();
    if( w == nullptr ) {
        // if there is no idle worker, fallback to the default.
        return cybozu::tcp_socket::on_writable(fd);
    }

    w->post_job(m_sendjob);
    return true;
}

void counter_socket::execute(const counter::request& cmd) {
    counter::response r(*this, cmd);

    if( cmd.status() != counter::status::OK ) {
        r.error( cmd.status() );
        return;
    }

    g_stats.ops[(std::size_t)cmd.command()].fetch_add(1);

    switch( cmd.command() ) {
    case counter::command::Noop:
        r.success();
        break;
    case counter::command::Get:
        cmd_get(cmd, r);
        break;
    case counter::command::Acquire:
        cmd_acquire(cmd, r);
        break;
    case counter::command::Release:
        cmd_release(cmd, r);
        break;
    case counter::command::Stats:
        r.stats();
        break;
    case counter::command::Dump:
        cmd_dump(r);
        break;
    default:
        cybozu::logger::info() << "not implemented";
        r.error( counter::status::UnknownCommand );
    }
}

void counter_socket::cmd_get(const counter::request& cmd, counter::response& r) {
    auto h = [this,&cmd,&r](const cybozu::hash_key& k, object& obj) -> bool {
        r.get(obj.consumption());
        return true;
    };
    if( ! m_hash.apply(cybozu::hash_key(cmd.name().p, cmd.name().len), h, nullptr) )
        r.error( counter::status::NotFound );
}

void counter_socket::cmd_acquire(const counter::request& cmd, counter::response& r) {
    uint32_t resources = cmd.resources();
    uint32_t maximum = cmd.maximum();
    auto h = [this,resources,maximum,&r](const cybozu::hash_key& k, object& obj) -> bool {
        if( ! obj.acquire(resources, maximum) ) {
            r.error( counter::status::ResourceNotAvailable );
            return true;
        }
        on_acquire(k, resources);
        r.acquire(resources);
        return true;
    };
    auto c = [this,resources,&r](const cybozu::hash_key& k) -> object {
        on_acquire(k, resources);
        r.acquire(resources);
        return object(resources);
    };
    m_hash.apply(cybozu::hash_key(cmd.name().p, cmd.name().len), h, c);
}

void counter_socket::cmd_release(const counter::request& cmd, counter::response& r) {
    uint32_t resources = cmd.resources();
    auto h = [this,resources,&r](const cybozu::hash_key& k, object& obj) -> bool {
        if( ! on_release(k, resources) ) {
            r.error( counter::status::NotAcquired );
            return true;
        }
        if( ! obj.release(resources) ) {
            cybozu::dump_stack();
            throw std::logic_error("<counter_socket::cmd_release> bug");
        }
        r.success();
        return true;
    };
    if( ! m_hash.apply(cybozu::hash_key(cmd.name().p, cmd.name().len), h, nullptr) )
        r.error( counter::status::NotFound );
}

void counter_socket::cmd_dump(counter::response& r) {
    auto pred = [this,&r](const cybozu::hash_key& k, object& obj) {
        r.dump(k.data(), k.length(), obj.consumption(), obj.max_consumption());
    };
    m_hash.foreach(pred);
    r.success();
}

void counter_socket::on_acquire(const cybozu::hash_key& k, std::uint32_t resources) {
    auto it = m_acquired_resources.find(&k);
    if( it != m_acquired_resources.end() ) {
        it->second += resources;
        return;
    }
    m_acquired_resources.emplace(&k, resources);
}

bool counter_socket::on_release(const cybozu::hash_key& k, std::uint32_t resources) {
    auto it = m_acquired_resources.find(&k);
    if( it == m_acquired_resources.end() )
        return false;
    if( it->second < resources )
        return false;
    if( it->second == resources ) {
        m_acquired_resources.erase(it);
        return true;
    }
    it->second -= resources;
    return true;
}

void counter_socket::release_all() {
    for( auto& res: m_acquired_resources ) {
        uint32_t count = res.second;
        auto h = [count](const cybozu::hash_key&, object& obj) -> bool {
            if( ! obj.release(count) ) {
                cybozu::dump_stack();
                throw std::logic_error("<counter_socket::release_all> release failed");
            }
            return true;
        };
        if( ! m_hash.apply(*res.first, h, nullptr) ) {
            cybozu::dump_stack();
            throw std::logic_error("<counter_socket::release_all> not found: "
                                   + res.first->str());
        }
    }
    m_acquired_resources.clear();
}

}} // namespace yrmcds::counter
