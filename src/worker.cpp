// (C) 2013 Cybozu.

#include "constants.hpp"
#include "replication.hpp"
#include "sockets.hpp"
#include "worker.hpp"

#include <cybozu/logger.hpp>

#include <iostream>
#include <sys/eventfd.h>

namespace mc = yrmcds::memcache;
using yrmcds::memcache::text_command;
using yrmcds::memcache::binary_command;
using yrmcds::memcache::binary_request;
using yrmcds::memcache::binary_status;

namespace {

using hash_map = cybozu::hash_map<yrmcds::object>;
const char NON_NUMERIC[] = "CLIENT_ERROR cannot increment or decrement non-numeric value\x0d\x0a";
const char NOT_LOCKED[] = "CLIENT_ERROR object is not locked or not found\x0d\x0a";

} // anonymous namespace

namespace yrmcds {

worker::worker(cybozu::hash_map<object>& m, slave_copier get_slaves):
    m_running(false), m_exit(false),
    m_hash(m), m_get_slaves(get_slaves),
    m_event(eventfd(0, EFD_CLOEXEC)), m_buffer(WORKER_BUFSIZE) {
    if( m_event == -1 )
        cybozu::throw_unix_error(errno, "eventfd");
    m_slaves.reserve(MAX_SLAVES);
}

worker::~worker() {
    ::close(m_event);
}

inline void worker::exec_cmd_bin(const binary_request& cmd) {
    mc::binary_response r(*m_socket, cmd);

    if( cmd.status() != binary_status::OK ) {
        r.error( cmd.status() );
        return;
    }

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
                m_socket->add_lock(k);
            }
            if( obj.expired() ) return false;
            if( cmd.exptime() != binary_request::EXPTIME_NONE )
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
            m_socket->remove_lock(k);
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
                m_socket->remove_lock(k);
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
        if( cmd.exptime() != binary_request::EXPTIME_NONE ) {
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
        if( cmd.exptime() != binary_request::EXPTIME_NONE ) {
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
            m_socket->add_lock(k);
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
            m_socket->remove_lock(k);
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
        m_socket->unlock_all();
        if( ! cmd.quiet() )
            r.success();
        break;
    case binary_command::Quit:
    case binary_command::QuitQ:
        m_socket->unlock_all();
        if( cmd.quiet() ) {
            m_socket->invalidate_and_close();
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
        default:
            r.stats_general(m_slaves.size());
        }
        break;
    default:
        cybozu::logger::info() << "not implemented";
        r.error( binary_status::UnknownCommand );
    }
}

inline void worker::exec_cmd_txt(const mc::text_request& cmd) {
    mc::text_response r(*m_socket);

    if( ! cmd.valid() ) {
        r.error();
        return;
    }

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
                m_socket->remove_lock(k);
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
            m_socket->add_lock(k);
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
            m_socket->remove_lock(k);
            r.ok();
            return true;
        };
        std::tie(p, len) = cmd.key();
        if( ! m_hash.apply(cybozu::hash_key(p, len), h, c) )
            r.send(NOT_LOCKED, sizeof(NOT_LOCKED) - 1, true);
        break;
    case text_command::UNLOCK_ALL:
        m_socket->unlock_all();
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
        m_socket->unlock_all();
        m_socket->invalidate_and_close();
        break;
    default:
        cybozu::logger::info() << "not implemented";
        r.error();
    }
}

void worker::run() {
    while ( true ) {
        wait();
        if( m_exit ) return;

        // set lock context for objects.
        g_context = m_socket->fileno();

        // load pending data
        m_buffer.reset();
        cybozu::dynbuf& pending = m_socket->get_buffer();
        if( ! pending.empty() ) {
            m_buffer.append(pending.data(), pending.size());
            pending.reset();
        }

        while( true ) {
            char* p = m_buffer.prepare(MAX_RECVSIZE);
            ssize_t n = ::recv(m_socket->fileno(), p, MAX_RECVSIZE, 0);
            if( n == -1 ) {
                if( errno == EAGAIN || errno == EWOULDBLOCK )
                    break;
                if( errno == EINTR )
                    continue;
                if( errno == ECONNRESET ) {
                    m_buffer.reset();
                    m_socket->unlock_all();
                    m_socket->invalidate_and_close();
                    break;
                }
                cybozu::throw_unix_error(errno, "recv");
            }
            if( n == 0 ) {
                m_buffer.reset();
                m_socket->unlock_all();
                m_socket->invalidate_and_close();
                break;
            }
            // if (n != -1) && (n != 0)
            m_buffer.consume(n);

            const char* head = m_buffer.data();
            std::size_t len = m_buffer.size();
            while( len > 0 ) {
                if( mc::is_binary_request(head) ) {
                    binary_request parser(head, len);
                    std::size_t c = parser.length();
                    if( c == 0 ) break;
                    head += c;
                    len -= c;
                    exec_cmd_bin(parser);
                } else {
                    mc::text_request parser(head, len);
                    std::size_t c = parser.length();
                    if( c == 0 ) break;
                    head += c;
                    len -= c;
                    exec_cmd_txt(parser);
                }
            }
            if( len > MAX_REQUEST_LENGTH ) {
                cybozu::logger::warning() << "denied too large request of "
                                          << len << " bytes.";
                m_buffer.reset();
                m_socket->unlock_all();
                m_socket->invalidate_and_close();
                break;
            }
            m_buffer.erase(head - m_buffer.data());
        }

        // recv returns EAGAIN, or some error happens.
        if( m_buffer.size() > 0 )
            pending.append(m_buffer.data(), m_buffer.size());

        m_socket->release();
    }
}

} // namespace yrmcds
