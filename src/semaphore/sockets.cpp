// (C) 2014 Cybozu.

#include "sockets.hpp"
#include "object.hpp"

#include <cybozu/util.hpp>

#include <cstddef>
#include <cstring>
#include <sys/socket.h>
#include <sys/types.h>

namespace yrmcds { namespace semaphore {

semaphore_socket::semaphore_socket(int fd,
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
            char* p = buf.prepare(MAX_RECVSIZE);
            ssize_t n = ::recv(m_fd, p, MAX_RECVSIZE, 0);
            if( n == -1 ) {
                if( errno == EAGAIN || errno == EWOULDBLOCK )
                    break;
                if( errno == EINTR )
                    continue;
                if( errno == ECONNRESET ) {
                    buf.reset();
                    release_all();
                    invalidate_and_close();
                    break;
                }
                cybozu::throw_unix_error(errno, "recv");
            }
            if( n == 0 ) {
                buf.reset();
                release_all();
                invalidate_and_close();
                break;
            }
            // if (n != -1) && (n != 0)
            buf.consume(n);

            const char* head = buf.data();
            std::size_t len = buf.size();
            while( len > 0 ) {
                semaphore::request parser(head, len);
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
        if( ! write_pending_data() )
            invalidate_and_close();
    };
}

semaphore_socket::~semaphore_socket() {
    // the destructor is the safe place to release remaining resources
    release_all();
}

bool semaphore_socket::on_readable() {
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

bool semaphore_socket::on_writable() {
    cybozu::worker* w = m_finder();
    if( w == nullptr ) {
        // if there is no idle worker, fallback to the default.
        return cybozu::tcp_socket::on_writable();
    }

    w->post_job(m_sendjob);
    return true;
}

void semaphore_socket::execute(const semaphore::request& cmd) {
    semaphore::response r(*this, cmd);

    if( cmd.status() != semaphore::status::OK ) {
        r.error( cmd.status() );
        return;
    }

    g_stats.ops[(std::size_t)cmd.command()].fetch_add(1);

    switch( cmd.command() ) {
    case semaphore::command::Noop:
        r.success();
        break;
    case semaphore::command::Get:
        cmd_get(cmd, r);
        break;
    case semaphore::command::Acquire:
        cmd_acquire(cmd, r);
        break;
    case semaphore::command::Release:
        cmd_release(cmd, r);
        break;
    case semaphore::command::Stats:
        r.stats();
        break;
    default:
        cybozu::logger::info() << "not implemented";
        r.error( semaphore::status::UnknownCommand );
    }
}

void semaphore_socket::cmd_get(const semaphore::request& cmd, semaphore::response& r) {
    auto h = [this,&cmd,&r](const cybozu::hash_key& k, object& obj) -> bool {
        r.get(obj.available());
        return true;
    };
    if( ! m_hash.apply(cybozu::hash_key(cmd.name().p, cmd.name().len), h, nullptr) )
        r.error( semaphore::status::NotFound );
}

void semaphore_socket::cmd_acquire(const semaphore::request& cmd, semaphore::response& r) {
    uint32_t resources = cmd.resources();
    uint32_t initial = cmd.initial();
    auto h = [this,resources,&r](const cybozu::hash_key& k, object& obj) -> bool {
        if( ! obj.acquire(resources) ) {
            r.error( semaphore::status::ResourceNotAvailable );
            return true;
        }
        on_acquire(k, resources);
        r.acquire(resources);
        return true;
    };
    auto c = [this,resources,initial,&r](const cybozu::hash_key& k) -> object {
        on_acquire(k, resources);
        r.acquire(resources);
        return object(initial - resources, initial);
    };
    m_hash.apply(cybozu::hash_key(cmd.name().p, cmd.name().len), h, c);
}

void semaphore_socket::cmd_release(const semaphore::request& cmd, semaphore::response& r) {
    uint32_t resources = cmd.resources();
    auto h = [this,resources,&r](const cybozu::hash_key& k, object& obj) -> bool {
        if( ! on_release(k, resources) ) {
            r.error( semaphore::status::NotAcquired );
            return true;
        }
        if( ! obj.release(resources) ) {
            cybozu::dump_stack();
            throw std::logic_error("<semaphore_socket::cmd_release> bug");
        }
        r.success();
        return true;
    };
    if( ! m_hash.apply(cybozu::hash_key(cmd.name().p, cmd.name().len), h, nullptr) )
        r.error( semaphore::status::NotFound );
}

void semaphore_socket::on_acquire(const cybozu::hash_key& k, std::uint32_t resources) {
    for( auto& res: m_acquired_resources ) {
        if( res.name == k ) {
            res.count += resources;
            return;
        }
    }
    m_acquired_resources.emplace_back(k, resources);
}

bool semaphore_socket::on_release(const cybozu::hash_key& k, std::uint32_t resources) {
    for( auto it = m_acquired_resources.begin(); it != m_acquired_resources.end(); ++it ) {
        if( it->name == k ) {
            if( it->count < resources )
                return false;
            if( it->count == resources ) {
                m_acquired_resources.erase(it);
                return true;
            }
            it->count -= resources;
            return true;
        }
    }
    return false;
}

void semaphore_socket::release_all() {
    for( auto& res: m_acquired_resources ) {
        uint32_t count = res.count;
        auto h = [count](const cybozu::hash_key&, object& obj) -> bool {
            if( ! obj.release(count) ) {
                cybozu::dump_stack();
                throw std::logic_error("<semaphore_socket::release_all> release failed");
            }
            return true;
        };
        if( ! m_hash.apply(res.name, h, nullptr) ) {
            cybozu::dump_stack();
            throw std::logic_error("<semaphore_socket::release_all> not found: "
                                   + res.name.get().str());
        }
    }
    m_acquired_resources.clear();
}

}} // namespace yrmcds::semaphore
