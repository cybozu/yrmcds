// (C) 2013 Cybozu.

#include "replication.hpp"
#include "sockets.hpp"

#include <cybozu/util.hpp>

#include <cstddef>
#include <cstring>
#include <sys/socket.h>
#include <sys/types.h>

namespace mc = yrmcds::memcache;
using mc::text_command;
using mc::binary_command;
using mc::binary_status;

namespace {

const char NON_NUMERIC[] = "CLIENT_ERROR cannot increment or decrement non-numeric value\x0d\x0a";
const char NOT_LOCKED[] = "CLIENT_ERROR object is not locked or not found\x0d\x0a";

} // anonymous namespace

namespace yrmcds {

using hash_map = cybozu::hash_map<object>;

memcache_socket::memcache_socket(int fd,
                                 const std::function<cybozu::worker*()>& finder,
                                 cybozu::hash_map<object>& hash,
                                 const std::vector<cybozu::tcp_socket*>& slaves)
    : cybozu::tcp_socket(fd),
      m_busy(false),
      m_finder(finder),
      m_hash(hash),
      m_pending(0),
      m_slaves_origin(slaves) {
    m_slaves.reserve(MAX_SLAVES);
    g_stats.curr_connections.fetch_add(1);
    g_stats.total_connections.fetch_add(1);

    m_recvjob = [this](cybozu::dynbuf& buf) {
        // set lock context for objects.
        g_context = m_fd;

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
                    unlock_all();
                    invalidate_and_close();
                    break;
                }
                cybozu::throw_unix_error(errno, "recv");
            }
            if( n == 0 ) {
                buf.reset();
                unlock_all();
                invalidate_and_close();
                break;
            }
            // if (n != -1) && (n != 0)
            buf.consume(n);

            const char* head = buf.data();
            std::size_t len = buf.size();
            while( len > 0 ) {
                if( mc::is_binary_request(head) ) {
                    mc::binary_request parser(head, len);
                    std::size_t c = parser.length();
                    if( c == 0 ) break;
                    head += c;
                    len -= c;
                    cmd_bin(parser);
                } else {
                    mc::text_request parser(head, len);
                    std::size_t c = parser.length();
                    if( c == 0 ) break;
                    head += c;
                    len -= c;
                    cmd_text(parser);
                }
            }
            if( len > MAX_REQUEST_LENGTH ) {
                cybozu::logger::warning() << "denied too large request of "
                                          << len << " bytes.";
                buf.reset();
                unlock_all();
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

    m_sendjob = [this](cybozu::dynbuf&) {
        if( ! write_pending_data() )
            invalidate_and_close();
    };
}

memcache_socket::~memcache_socket() {
    // the destructor is the safe place to release remaining locks.
    for( auto& ref: m_locks ) {
        m_hash.apply(ref.get(),
                     [](const cybozu::hash_key&, object& obj) -> bool {
                         obj.unlock(true);
                         return true;
                     }, nullptr);
    }
}

bool memcache_socket::on_readable() {
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

    // copy the current list of slaves
    m_slaves = m_slaves_origin;

    m_busy.store(true, std::memory_order_release);
    w->post_job(m_recvjob);
    return true;
}

bool memcache_socket::on_writable() {
    cybozu::worker* w = m_finder();
    if( w == nullptr ) {
        // if there is no idle worker, fallback to the default.
        return cybozu::tcp_socket::on_writable();
    }

    w->post_job(m_sendjob);
    return true;
}

void memcache_socket::cmd_bin(const memcache::binary_request& cmd) {
    mc::binary_response r(*this, cmd);

    if( cmd.status() != binary_status::OK ) {
        r.error( cmd.status() );
        return;
    }

    g_stats.bin_ops[(std::size_t)cmd.command()].fetch_add(1);

    const char* p;
    std::size_t len;
    hash_map::handler h = nullptr;
    hash_map::creator c = nullptr;
    std::function<void(const cybozu::hash_key&)> repl = nullptr;
    std::function<bool(const cybozu::hash_key&, object&)> pred;

    switch( cmd.command() ) {
    case binary_command::Get:
    case binary_command::GetQ:
    case binary_command::GetK:
    case binary_command::GetKQ:
    case binary_command::GaT:
    case binary_command::GaTQ:
    case binary_command::GaTK:
    case binary_command::GaTKQ:
    case binary_command::LaG:
    case binary_command::LaGQ:
    case binary_command::LaGK:
    case binary_command::LaGKQ:
        h = [this,&cmd,&r](const cybozu::hash_key& k, object& obj) -> bool {
            if( (binary_command::LaG <= cmd.command()) &&
                (cmd.command() <= binary_command::LaGKQ) ) {
                if( obj.locked() ) {
                    r.error( binary_status::Locked );
                    return true;
                }
                obj.lock();
                add_lock(k);
            }
            if( obj.expired() ) return false;
            if( cmd.exptime() != mc::binary_request::EXPTIME_NONE )
                obj.touch( cmd.exptime() );
            cybozu::dynbuf buf(0);
            const cybozu::dynbuf& data = obj.data(buf);
            if( cmd.command() == binary_command::Get ||
                cmd.command() == binary_command::GetQ ||
                cmd.command() == binary_command::GaT ||
                cmd.command() == binary_command::GaTQ ||
                cmd.command() == binary_command::LaG ||
                cmd.command() == binary_command::LaGQ ) {
                r.get(obj.flags(), data, obj.cas_unique(), ! cmd.quiet());
            } else {
                r.get(obj.flags(), data, obj.cas_unique(), ! cmd.quiet(),
                      k.data(), k.length());
            }
            return true;
        };
        std::tie(p, len) = cmd.key();
        if( ! m_hash.apply(cybozu::hash_key(p, len), h, c) ) {
            if( ! cmd.quiet() || cmd.command() == binary_command::LaGQ )
                r.error( binary_status::NotFound );
        }
        break;
    case binary_command::Set:
    case binary_command::SetQ:
    case binary_command::Add:
    case binary_command::AddQ:
    case binary_command::Replace:
    case binary_command::ReplaceQ:
        std::tie(p, len) = cmd.key();
        if( len > MAX_KEY_LENGTH ) {
            r.error( binary_status::Invalid );
            return;
        }
        if( std::get<1>(cmd.data()) > g_config.max_data_size() ) {
            r.error( binary_status::TooLargeValue );
            return;
        }
        h = [this,&cmd,&r](const cybozu::hash_key& k, object& obj) -> bool {
            if( obj.locked_by_other() ) {
                r.error( binary_status::Locked );
                return true;
            }
            if( obj.expired() ) {
                if( cmd.cas_unique() != 0 ||
                    cmd.command() == binary_command::Replace ||
                    cmd.command() == binary_command::ReplaceQ )
                    return false;
            } else if( cmd.command() == binary_command::Add ||
                       cmd.command() == binary_command::AddQ ) {
                return false;
            }
            if( cmd.cas_unique() != 0 &&
                cmd.cas_unique() != obj.cas_unique() ) {
                r.error( binary_status::Exists );
                return true;
            }
            const char* p2;
            std::size_t len2;
            std::tie(p2, len2) = cmd.data();
            obj.set(p2, len2, cmd.flags(), cmd.exptime());
            if( ! cmd.quiet() )
                r.set( obj.cas_unique() );
            if( ! m_slaves.empty() )
                repl_object(m_slaves, k, obj);
            return true;
        };
        if( cmd.command() != binary_command::Replace &&
            cmd.command() != binary_command::ReplaceQ &&
            cmd.cas_unique() == 0 ) {
            c = [this,&cmd,&r](const cybozu::hash_key& k) -> object {
                const char* p2;
                std::size_t len2;
                std::tie(p2, len2) = cmd.data();
                object o(p2, len2, cmd.flags(), cmd.exptime());
                if( ! cmd.quiet() )
                    r.set( o.cas_unique() );
                if( ! m_slaves.empty() )
                    repl_object(m_slaves, k, o);
                return std::move(o);
            };
        }
        if( ! m_hash.apply(cybozu::hash_key(p, len), h, c) ) {
            if( cmd.cas_unique() != 0 ) {
                r.error( binary_status::NotFound );
            } else {
                r.error( binary_status::NotStored );
            }
        }
        break;
    case binary_command::RaU:
    case binary_command::RaUQ:
        std::tie(p, len) = cmd.key();
        if( len > MAX_KEY_LENGTH ) {
            r.error( binary_status::Invalid );
            return;
        }
        if( std::get<1>(cmd.data()) > g_config.max_data_size() ) {
            r.error( binary_status::TooLargeValue );
            return;
        }
        h = [this,&cmd,&r](const cybozu::hash_key& k, object& obj) -> bool {
            if( ! obj.locked_by_self() ) {
                r.error( binary_status::NotLocked );
                return true;
            }
            if( cmd.cas_unique() != 0 &&
                cmd.cas_unique() != obj.cas_unique() ) {
                r.error( binary_status::Exists );
                return true;
            }
            const char* p2;
            std::size_t len2;
            std::tie(p2, len2) = cmd.data();
            obj.set(p2, len2, cmd.flags(), cmd.exptime());
            obj.unlock();
            remove_lock(k);
            if( ! cmd.quiet() )
                r.set( obj.cas_unique() );
            if( ! m_slaves.empty() )
                repl_object(m_slaves, k, obj);
            return true;
        };
        if( ! m_hash.apply(cybozu::hash_key(p, len), h, c) ) {
            if( cmd.cas_unique() != 0 ) {
                r.error( binary_status::NotFound );
            } else {
                r.error( binary_status::NotStored );
            }
        }
        break;
    case binary_command::Append:
    case binary_command::AppendQ:
        std::tie(p, len) = cmd.key();
        h = [this,&cmd,&r](const cybozu::hash_key& k, object& obj) -> bool {
            if( obj.locked_by_other() ) {
                r.error( binary_status::Locked );
                return true;
            }
            if( obj.expired() ) return false;
            const char* p2;
            std::size_t len2;
            std::tie(p2, len2) = cmd.data();
            obj.append(p2, len2);
            if( ! cmd.quiet() )
                r.success();
            if( ! m_slaves.empty() )
                repl_object(m_slaves, k, obj);
            return true;
        };
        if( ! m_hash.apply(cybozu::hash_key(p, len), h, c) )
            r.error( binary_status::NotFound );
        break;
    case binary_command::Prepend:
    case binary_command::PrependQ:
        std::tie(p, len) = cmd.key();
        h = [this,&cmd,&r](const cybozu::hash_key& k, object& obj) -> bool {
            if( obj.locked_by_other() ) {
                r.error( binary_status::Locked );
                return true;
            }
            if( obj.expired() ) return false;
            const char* p2;
            std::size_t len2;
            std::tie(p2, len2) = cmd.data();
            obj.prepend(p2, len2);
            if( ! cmd.quiet() )
                r.success();
            if( ! m_slaves.empty() )
                repl_object(m_slaves, k, obj);
            return true;
        };
        if( ! m_hash.apply(cybozu::hash_key(p, len), h, c) )
            r.error( binary_status::NotFound );
        break;
    case binary_command::Delete:
    case binary_command::DeleteQ:
        std::tie(p, len) = cmd.key();
        pred = [this,&cmd,&r](const cybozu::hash_key& k, object& obj) -> bool {
            if( obj.locked_by_other() ) {
                r.error( binary_status::Locked );
                return false;
            }
            if( obj.locked_by_self() )
                remove_lock(k);
            if( ! cmd.quiet() )
                r.success();
            if( ! m_slaves.empty() )
                repl_delete(m_slaves, k);
            return true;
        };
        if( ! m_hash.remove_if(cybozu::hash_key(p, len), pred) &&
            ! cmd.quiet() )
            r.error( binary_status::NotFound );
        break;
    case binary_command::Increment:
    case binary_command::IncrementQ:
        std::tie(p, len) = cmd.key();
        h = [this,&cmd,&r](const cybozu::hash_key& k, object& obj) -> bool {
            if( obj.locked_by_other() ) {
                r.error( binary_status::Locked );
                return true;
            }
            if( obj.expired() ) return false;
            try {
                std::uint64_t n = obj.incr( cmd.value() );
                if( ! cmd.quiet() )
                    r.incdec( n, obj.cas_unique() );
                if( ! m_slaves.empty() )
                    repl_object(m_slaves, k, obj);
            } catch( const object::not_a_number& ) {
                r.error( binary_status::NonNumeric );
            }
            return true;
        };
        if( cmd.exptime() != mc::binary_request::EXPTIME_NONE ) {
            c = [this,&cmd,&r](const cybozu::hash_key& k) -> object {
                object o(cmd.initial(), cmd.exptime());
                if( ! cmd.quiet() )
                    r.incdec( cmd.initial(), o.cas_unique() );
                if( ! m_slaves.empty() )
                    repl_object(m_slaves, k, o);
                return std::move(o);
            };
        }
        if( ! m_hash.apply(cybozu::hash_key(p, len), h, c) )
            r.error( binary_status::NotFound );
        break;
    case binary_command::Decrement:
    case binary_command::DecrementQ:
        std::tie(p, len) = cmd.key();
        h = [this,&cmd,&r](const cybozu::hash_key& k, object& obj) -> bool {
            if( obj.locked_by_other() ) {
                r.error( binary_status::Locked );
                return true;
            }
            if( obj.expired() ) return false;
            try {
                std::uint64_t n = obj.decr( cmd.value() );
                if( ! cmd.quiet() )
                    r.incdec( n, obj.cas_unique() );
                if( ! m_slaves.empty() )
                    repl_object(m_slaves, k, obj);
            } catch( const object::not_a_number& ) {
                r.error( binary_status::NonNumeric );
            }
            return true;
        };
        if( cmd.exptime() != mc::binary_request::EXPTIME_NONE ) {
            c = [this,&cmd,&r](const cybozu::hash_key& k) -> object {
                object o(cmd.initial(), cmd.exptime());
                if( ! cmd.quiet() )
                    r.incdec( cmd.initial(), o.cas_unique() );
                if( ! m_slaves.empty() )
                    repl_object(m_slaves, k, o);
                return std::move(o);
            };
        }
        if( ! m_hash.apply(cybozu::hash_key(p, len), h, c) )
            r.error( binary_status::NotFound );
        break;
    case binary_command::Touch:
        std::tie(p, len) = cmd.key();
        h = [this,&cmd](const cybozu::hash_key& k, object& obj) -> bool {
            if( obj.expired() ) return false;
            obj.touch( cmd.exptime() );
            if( ! m_slaves.empty() )
                repl_object(m_slaves, k, obj, false);
            return true;
        };
        if( m_hash.apply(cybozu::hash_key(p, len), h, c) ) {
            r.success();
        } else {
            r.error( binary_status::NotFound );
        }
        break;
    case binary_command::Lock:
    case binary_command::LockQ:
        h = [this,&cmd,&r](const cybozu::hash_key& k, object& obj) -> bool {
            if( obj.expired() ) return false;
            if( obj.locked() ) {
                r.error( binary_status::Locked );
                return true;
            }
            obj.lock();
            add_lock(k);
            if( ! cmd.quiet() )
                r.success();
            return true;
        };
        std::tie(p, len) = cmd.key();
        if( ! m_hash.apply(cybozu::hash_key(p, len), h, c) )
            r.error( binary_status::NotFound );
        break;
    case binary_command::Unlock:
    case binary_command::UnlockQ:
        h = [this,&cmd,&r](const cybozu::hash_key& k, object& obj) -> bool {
            if( ! obj.locked_by_self() ) {
                r.error( binary_status::NotLocked );
                return true;
            }
            obj.unlock();
            remove_lock(k);
            if( ! cmd.quiet() )
                r.success();
            return true;
        };
        std::tie(p, len) = cmd.key();
        if( ! m_hash.apply(cybozu::hash_key(p, len), h, c) )
            r.error( binary_status::NotFound );
        break;
    case binary_command::UnlockAll:
    case binary_command::UnlockAllQ:
        unlock_all();
        if( ! cmd.quiet() )
            r.success();
        break;
    case binary_command::Quit:
    case binary_command::QuitQ:
        unlock_all();
        if( cmd.quiet() ) {
            invalidate_and_close();
        } else {
            r.quit();
        }
        break;
    case binary_command::Flush:
    case binary_command::FlushQ:
        g_stats.flush_time.store( cmd.exptime() );
        if( ! cmd.quiet() )
            r.success();
        break;
    case binary_command::Noop:
        r.success();
        break;
    case binary_command::Version:
        r.version();
        break;
    case binary_command::Stat:
        switch( cmd.stats() ) {
        case mc::stats_t::SETTINGS:
            r.stats_settings();
            break;
        case mc::stats_t::ITEMS:
            r.stats_items();
            break;
        case mc::stats_t::SIZES:
            r.stats_sizes();
            break;
        case mc::stats_t::OPS:
            r.stats_ops();
            break;
        default:
            r.stats_general(m_slaves.size());
        }
        break;
    default:
        cybozu::logger::info() << "not implemented";
        r.error( binary_status::UnknownCommand );
    }
}

void memcache_socket::cmd_text(const memcache::text_request& cmd) {
    mc::text_response r(*this);

    if( ! cmd.valid() ) {
        r.error();
        return;
    }

    g_stats.text_ops[(std::size_t)cmd.command()].fetch_add(1);

    const char* p;
    std::size_t len;
    hash_map::handler h = nullptr;
    hash_map::creator c = nullptr;
    std::function<bool(const cybozu::hash_key&, object&)> pred;

    switch( cmd.command() ) {
    case text_command::SET:
    case text_command::ADD:
    case text_command::REPLACE:
        std::tie(p, len) = cmd.key();
        if( len > MAX_KEY_LENGTH ) {
            r.error();
            return;
        }
        if( std::get<1>(cmd.data()) > g_config.max_data_size() ) {
            r.error();
            return;
        }
        h = [this,&cmd,&r](const cybozu::hash_key& k, object& obj) -> bool {
            if( obj.locked_by_other() ) {
                if( ! cmd.no_reply() )
                    r.locked();
                return true;
            }
            if( obj.expired() ) {
                if( cmd.command() == text_command::REPLACE )
                    return false;
            } else if( cmd.command() == text_command::ADD ) {
                return false;
            }
            const char* p2;
            std::size_t len2;
            std::tie(p2, len2) = cmd.data();
            obj.set(p2, len2, cmd.flags(), cmd.exptime());
            if( ! cmd.no_reply() )
                r.stored();
            if( ! m_slaves.empty() )
                repl_object(m_slaves, k, obj);
            return true;
        };
        if( cmd.command() != text_command::REPLACE ) {
            c = [this,&cmd,&r](const cybozu::hash_key& k) -> object {
                const char* p2;
                std::size_t len2;
                std::tie(p2, len2) = cmd.data();
                object o(p2, len2, cmd.flags(), cmd.exptime());
                if( ! cmd.no_reply() )
                    r.stored();
                if( ! m_slaves.empty() )
                    repl_object(m_slaves, k, o);
                return std::move(o);
            };
        }
        if( ! m_hash.apply(cybozu::hash_key(p, len), h, c) && ! cmd.no_reply() )
            r.not_stored();
        break;
    case text_command::APPEND:
        std::tie(p, len) = cmd.key();
        h = [this,&cmd,&r](const cybozu::hash_key& k, object& obj) -> bool {
            if( obj.locked_by_other() ) {
                if( ! cmd.no_reply() )
                    r.locked();
                return true;
            }
            if( obj.expired() ) return false;
            const char* p2;
            std::size_t len2;
            std::tie(p2, len2) = cmd.data();
            obj.append(p2, len2);
            if( ! cmd.no_reply() )
                r.stored();
            if( ! m_slaves.empty() )
                repl_object(m_slaves, k, obj);
            return true;
        };
        if( ! m_hash.apply(cybozu::hash_key(p, len), h, c) && ! cmd.no_reply() )
            r.not_stored();
        break;
    case text_command::PREPEND:
        std::tie(p, len) = cmd.key();
        h = [this,&cmd,&r](const cybozu::hash_key& k, object& obj) -> bool {
            if( obj.locked_by_other() ) {
                if( ! cmd.no_reply() )
                    r.locked();
                return true;
            }
            if( obj.expired() ) return false;
            const char* p2;
            std::size_t len2;
            std::tie(p2, len2) = cmd.data();
            obj.prepend(p2, len2);
            if( ! cmd.no_reply() )
                r.stored();
            if( ! m_slaves.empty() )
                repl_object(m_slaves, k, obj);
            return true;
        };
        if( ! m_hash.apply(cybozu::hash_key(p, len), h, c) && ! cmd.no_reply() )
            r.not_stored();
        break;
    case text_command::CAS:
        std::tie(p, len) = cmd.key();
        h = [this,&cmd,&r](const cybozu::hash_key& k, object& obj) -> bool {
            if( obj.locked_by_other() ) {
                if( ! cmd.no_reply() )
                    r.locked();
                return true;
            }
            if( obj.expired() ) return false;
            if( obj.cas_unique() != cmd.cas_unique() ) {
                if( ! cmd.no_reply() )
                    r.exists();
                return true;
            }
            const char* p2;
            std::size_t len2;
            std::tie(p2, len2) = cmd.data();
            obj.set(p2, len2, cmd.flags(), cmd.exptime());
            if( ! cmd.no_reply() )
                r.stored();
            if( ! m_slaves.empty() )
                repl_object(m_slaves, k, obj);
            return true;
        };
        if( ! m_hash.apply(cybozu::hash_key(p, len), h, c) && ! cmd.no_reply() )
            r.not_found();
        break;
    case text_command::INCR:
        std::tie(p, len) = cmd.key();
        h = [this,&cmd,&r](const cybozu::hash_key& k, object& obj) -> bool {
            if( obj.locked_by_other() ) {
                if( ! cmd.no_reply() )
                    r.locked();
                return true;
            }
            if( obj.expired() ) return false;
            try {
                std::uint64_t n = obj.incr( cmd.value() );
                if( ! cmd.no_reply() ) {
                    char buf[24];
                    int numlen = snprintf(buf, sizeof(buf), "%llu\x0d\x0a",
                                          (unsigned long long)n);
                    r.send(buf, numlen, true);
                }
                if( ! m_slaves.empty() )
                    repl_object(m_slaves, k, obj);
            } catch( const object::not_a_number& ) {
                if( ! cmd.no_reply() )
                    r.send(NON_NUMERIC, sizeof(NON_NUMERIC) - 1, true);
            }
            return true;
        };
        if( ! m_hash.apply(cybozu::hash_key(p, len), h, c) && ! cmd.no_reply() )
            r.not_found();
        break;
    case text_command::DECR:
        std::tie(p, len) = cmd.key();
        h = [this,&cmd,&r](const cybozu::hash_key& k, object& obj) -> bool {
            if( obj.locked_by_other() ) {
                if( ! cmd.no_reply() )
                    r.locked();
                return true;
            }
            if( obj.expired() ) return false;
            try {
                std::uint64_t n = obj.decr( cmd.value() );
                if( ! cmd.no_reply() ) {
                    char buf[24];
                    int numlen = snprintf(buf, sizeof(buf), "%llu\x0d\x0a",
                                          (unsigned long long)n);
                    r.send(buf, numlen, true);
                }
                if( ! m_slaves.empty() )
                    repl_object(m_slaves, k, obj);
            } catch( const object::not_a_number& ) {
                if( ! cmd.no_reply() )
                    r.send(NON_NUMERIC, sizeof(NON_NUMERIC) - 1, true);
            }
            return true;
        };
        if( ! m_hash.apply(cybozu::hash_key(p, len), h, c) && ! cmd.no_reply() )
            r.not_found();
        break;
    case text_command::TOUCH:
        std::tie(p, len) = cmd.key();
        h = [this,&cmd](const cybozu::hash_key& k, object& obj) -> bool {
            if( obj.expired() ) return false;
            obj.touch( cmd.exptime() );
            if( ! m_slaves.empty() )
                repl_object(m_slaves, k, obj, false);
            return true;
        };
        if( m_hash.apply(cybozu::hash_key(p, len), h, c) ) {
            if( ! cmd.no_reply() )
                r.touched();
        } else {
            if( ! cmd.no_reply() )
                r.not_found();
        }
        break;
    case text_command::DELETE:
        std::tie(p, len) = cmd.key();
        pred = [this,&cmd,&r](const cybozu::hash_key& k, object& obj) -> bool {
            if( obj.locked_by_other() ) {
                if( ! cmd.no_reply() )
                    r.locked();
                return false;
            }
            if( obj.locked_by_self() )
                remove_lock(k);
            if( ! cmd.no_reply() )
                r.deleted();
            if( ! m_slaves.empty() )
                repl_delete(m_slaves, k);
            return true;
        };
        if( ! m_hash.remove_if(cybozu::hash_key(p, len), pred) &&
            ! cmd.no_reply() )
            r.not_found();
        break;
    case text_command::LOCK:
        h = [this,&cmd,&r](const cybozu::hash_key& k, object& obj) -> bool {
            if( obj.expired() ) return false;
            if( obj.locked() ) {
                r.locked();
                return true;
            }
            obj.lock();
            add_lock(k);
            r.ok();
            return true;
        };
        std::tie(p, len) = cmd.key();
        if( ! m_hash.apply(cybozu::hash_key(p, len), h, c) )
            r.not_found();
        break;
    case text_command::UNLOCK:
        h = [this,&cmd,&r](const cybozu::hash_key& k, object& obj) -> bool {
            if( ! obj.locked_by_self() ) return false;
            obj.unlock();
            remove_lock(k);
            r.ok();
            return true;
        };
        std::tie(p, len) = cmd.key();
        if( ! m_hash.apply(cybozu::hash_key(p, len), h, c) )
            r.send(NOT_LOCKED, sizeof(NOT_LOCKED) - 1, true);
        break;
    case text_command::UNLOCK_ALL:
        unlock_all();
        r.ok();
        break;
    case text_command::GET:
    case text_command::GETS:
        h = [&cmd,&r](const cybozu::hash_key& k, object& obj) -> bool {
            if( obj.expired() ) return false;
            cybozu::dynbuf buf(0);
            const cybozu::dynbuf& data = obj.data(buf);
            if( cmd.command() == text_command::GETS ) {
                r.value(k, obj.flags(), data, obj.cas_unique());
            } else {
                r.value(k, obj.flags(), data);
            }
            return true;
        };
        for( mc::item it = cmd.first_key();
             it != mc::text_request::eos; it = cmd.next_key(it) ) {
            std::tie(p, len) = it;
            m_hash.apply(cybozu::hash_key(p, len), h, nullptr);
        }
        r.end();
        break;
    case text_command::SLABS:
        r.ok();
        break;
    case text_command::STATS:
        switch( cmd.stats() ) {
        case mc::stats_t::SETTINGS:
            r.stats_settings();
            break;
        case mc::stats_t::ITEMS:
            r.stats_items();
            break;
        case mc::stats_t::SIZES:
            r.stats_sizes();
            break;
        case mc::stats_t::OPS:
            r.stats_ops();
            break;
        default:
            r.stats_general(m_slaves.size());
        }
        r.end();
        break;
    case text_command::FLUSH_ALL:
        g_stats.flush_time.store( cmd.exptime() );
        if( ! cmd.no_reply() )
            r.ok();
        break;
    case text_command::VERSION:
        r.version();
        break;
    case text_command::VERBOSITY:
        cybozu::logger::set_threshold(cmd.verbosity());
        if( ! cmd.no_reply() )
            r.ok();
        break;
    case text_command::QUIT:
        unlock_all();
        invalidate_and_close();
        break;
    default:
        cybozu::logger::info() << "not implemented";
        r.error();
    }
}

bool repl_socket::on_readable() {
    // recv and drop.
    while( true ) {
        ssize_t n = ::recv(m_fd, &m_recvbuf[0], MAX_RECVSIZE, 0);
        if( n == -1 ) {
            if( errno == EAGAIN || errno == EWOULDBLOCK )
                break;
            if( errno == EINTR )
                continue;
            if( errno == ECONNRESET )
                return invalidate();
            cybozu::throw_unix_error(errno, "recv");
        }
        if( n == 0 )
            return invalidate();
    }
    return true;
}

bool repl_socket::on_writable() {
    cybozu::worker* w = m_finder();
    if( w == nullptr ) {
        // if there is no idle worker, fallback to the default.
        return cybozu::tcp_socket::on_writable();
    }

    w->post_job(m_sendjob);
    return true;
}

bool repl_client_socket::on_readable() {
    while( true ) {
        char* p = m_recvbuf.prepare(MAX_RECVSIZE);
        ssize_t n = ::recv(m_fd, p, MAX_RECVSIZE, 0);
        if( n == -1 ) {
            if( errno == EAGAIN || errno == EWOULDBLOCK )
                break;
            if( errno == EINTR )
                continue;
            if( errno == ECONNRESET ) {
                m_reactor->quit();
                return invalidate();
            }
            cybozu::throw_unix_error(errno, "recv");
        }
        if( n == 0 ) {
            m_reactor->quit();
            return invalidate();
        }
        m_recvbuf.consume(n);

        std::size_t c = repl_recv(m_recvbuf.data(), m_recvbuf.size(), m_hash);
        m_recvbuf.erase(c);
    }
    return true;
}

} // namespace yrmcds
