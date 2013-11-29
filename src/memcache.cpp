// (C) 2013 Cybozu.

#include "config.hpp"
#include "memcache.hpp"
#include "stats.hpp"

#include <cybozu/util.hpp>

#include <algorithm>
#include <cerrno>
#include <limits>
#include <stdlib.h>
#include <sstream>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

namespace {

using namespace yrmcds;
enum std::memory_order relaxed = std::memory_order_relaxed;

const char CRLF[] = "\x0d\x0a";
const char CR = '\x0d';
const char LF = '\x0a';
const char SP = '\x20';
const char VALUE[] = "VALUE ";

const char STATUS_NOT_FOUND[] = "Not found";
const char STATUS_EXISTS[] = "Exists";
const char STATUS_TOO_LARGE[] = "Too large value";
const char STATUS_INVALID[] = "Invalid request";
const char STATUS_NOT_STORED[] = "Not stored";
const char STATUS_NON_NUMERIC[] = "Non-numeric value";
const char STATUS_LOCKED[] = "Locked";
const char STATUS_NOT_LOCKED[] = "Not locked";
const char STATUS_UNKNOWN[] = "Unknown command";
const char STATUS_OOM[] = "Out of memory";

const std::time_t EXPTIME_THRESHOLD = 60*60*24*30;

const std::size_t BINARY_HEADER_SIZE = 24;

inline const char* cfind(const char* p, char c, std::size_t len) {
    return (const char*)std::memchr(p, c, len);
}

template<typename UInt>
inline UInt to_uint(const char* p, bool& result) {
    result = false;
    char* end;
    unsigned long long i = strtoull(p, &end, 10);
    if( i == 0 && p == end ) return 0;
    if( i == std::numeric_limits<unsigned long long>::max() &&
        errno == ERANGE ) return 0;
    char c = *end;
    if( c != CR && c != LF && c != SP ) return 0;
    if( i > std::numeric_limits<UInt>::max() ) return 0;
    result = true;
    return static_cast<UInt>(i);
}

inline std::time_t binary_exptime(const char* p) noexcept {
    std::uint32_t t;
    cybozu::ntoh(p, t);
    if( t == 0 ) return 0;
    if( t == 0xffffffffUL )
        return memcache::binary_request::EXPTIME_NONE;
    if( t > EXPTIME_THRESHOLD )
        return t;
    return g_stats.current_time.load(relaxed) + t;
}

} // anonymous namespace

namespace yrmcds { namespace memcache {

constexpr item text_request::eos;

inline void
text_request::parse_flushall(const char* b, const char* e) noexcept {
    m_exptime = g_stats.current_time.load(relaxed);

    while( *b == SP ) ++b;
    if( b == e ) {
        m_valid = true;
        return;
    }

    if( '0' <= *b && *b <= '9' ) {
        const char* exp_end = cfind(b, SP, e-b);
        if( exp_end == nullptr ) exp_end = e;
        bool result;
        std::time_t t = to_uint<std::time_t>(b, result);
        if( ! result ) return;
        m_exptime = (t > EXPTIME_THRESHOLD) ? t :
            (g_stats.current_time.load(relaxed) + t);
        b = exp_end;
    }

    while( *b == SP ) ++b;
    if( b != e ) {
        // noreply ?
        const char* we = cfind(b, SP, e-b);
        std::size_t noreply_len = (we != nullptr) ? (we - b) : (e - b);
        if( noreply_len != 7 ) return;
        if( std::memcmp(b, "noreply", 7) != 0 ) return;
        m_no_reply = true;
        b = (we != nullptr) ? we : e;
    }

    while( *b == SP ) ++b;
    if( b != e ) return; // garbage left

    m_valid = true;
}

inline void
text_request::parse_verbosity(const char* b, const char* e) noexcept {
    while( *b == SP ) ++b;
    if( b == e ) return;
    const char* vb_end = cfind(b, SP, e-b);
    if( vb_end == nullptr ) vb_end = e;
    std::size_t len = vb_end - b;
    if( len == 0 ) return; // invalid

    if( len == 5 ) {
        if( std::memcmp("error", b, 5) == 0 ) {
            m_verbosity = cybozu::severity::error;
        } else if( std::memcmp("debug", b, 5) == 0 ) {
            m_verbosity = cybozu::severity::debug;
        }
    }
    if( len == 7 && std::memcmp("warning", b, 7) == 0 )
        m_verbosity = cybozu::severity::warning;
    if( len == 4 && std::memcmp("info", b, 4) == 0 )
        m_verbosity = cybozu::severity::info;

    b = vb_end;

    while( *b == SP ) ++b;
    if( b != e ) {
        // noreply ?
        const char* we = cfind(b, SP, e-b);
        std::size_t noreply_len = (we != nullptr) ? (we - b) : (e - b);
        if( noreply_len != 7 ) return;
        if( std::memcmp(b, "noreply", 7) != 0 ) return;
        m_no_reply = true;
        b = (we != nullptr) ? we : e;
    }

    while( *b == SP ) ++b;
    if( b != e ) return; // garbage left

    m_valid = true;
}

inline void
text_request::parse_storage(const char* b, const char* e, bool is_cas) noexcept {
    while( *b == SP ) ++b;
    if( b == e ) return;
    const char* key_end = cfind(b, SP, e-b);
    if( key_end == nullptr ) return;
    m_key = item(b, key_end-b);
    b = key_end;

    while( *b == SP ) ++b;
    if( b == e ) return;
    const char* flags_end = cfind(b, SP, e-b);
    if( flags_end == nullptr ) return;
    bool result;
    m_flags = to_uint<std::uint32_t>(b, result);
    if( ! result ) return;
    b = flags_end;

    while( *b == SP ) ++b;
    if( b == e ) return;
    const char* exptime_end = cfind(b, SP, e-b);
    if( exptime_end == nullptr ) return;
    std::time_t t = to_uint<std::time_t>(b, result);
    if( ! result ) return;
    if( t != 0 )
        m_exptime = (t > EXPTIME_THRESHOLD) ? t :
            (g_stats.current_time.load(relaxed) + t);
    b = exptime_end;

    while( *b == SP ) ++b;
    if( b == e ) return;
    std::uint32_t nbytes = to_uint<std::uint32_t>(b, result);
    if( ! result ) return;
    const char* bytes_end = cfind(b, SP, e-b);
    b = (bytes_end == nullptr) ? e : bytes_end;

    if( is_cas ) {
        while( *b == SP ) ++b;
        if( b == e ) return;
        m_cas_unique = to_uint<std::uint64_t>(b, result);
        if( ! result ) return;
        const char* cas_end = cfind(b, SP, e-b);
        b = (cas_end == nullptr) ? e : cas_end;
    }

    while( *b == SP ) ++b;
    if( b != e ) {
        // noreply ?
        const char* we = cfind(b, SP, e-b);
        std::size_t noreply_len = (we != nullptr) ? (we - b) : (e - b);
        if( noreply_len != 7 ) return;
        if( std::memcmp(b, "noreply", 7) != 0 ) return;
        m_no_reply = true;
        b = (we != nullptr) ? we : e;
    }

    while( *b == SP ) ++b;
    if( b != e ) return; // garbage left

    // here, parsing was successful.  check data length.
    b += 2; // CRLF
    m_request_len = (b - m_p) + nbytes + 2;
    if( m_len < m_request_len ) {
        m_request_len = 0;
        return;
    }

    // check CRLF following the data
    if( *(b+nbytes) != CR || *(b+nbytes+1) != LF )
        return;

    // passed all checks.
    m_data = item(b, nbytes);
    m_valid = true;
}

inline void
text_request::parse_delete(const char* b, const char* e) noexcept {
    while( *b == SP ) ++b;
    if( b == e ) return;
    const char* key_end = cfind(b, SP, e-b);
    if( key_end == nullptr ) key_end = e;
    m_key = item(b, key_end-b);
    b = key_end;

    while( *b == SP ) ++b;
    if( b != e ) {
        // noreply ?
        const char* we = cfind(b, SP, e-b);
        std::size_t noreply_len = (we != nullptr) ? (we - b) : (e - b);
        if( noreply_len != 7 ) return;
        if( std::memcmp(b, "noreply", 7) != 0 ) return;
        m_no_reply = true;
        b = (we != nullptr) ? we : e;
    }

    while( *b == SP ) ++b;
    if( b != e ) return; // garbage left

    m_valid = true;
}

inline void
text_request::parse_touch(const char* b, const char* e) noexcept {
    while( *b == SP ) ++b;
    if( b == e ) return;
    const char* key_end = cfind(b, SP, e-b);
    if( key_end == nullptr ) return;
    m_key = item(b, key_end-b);
    b = key_end;

    while( *b == SP ) ++b;
    if( b == e ) return;
    const char* exptime_end = cfind(b, SP, e-b);
    if( exptime_end == nullptr ) exptime_end = e;
    bool result;
    std::time_t t = to_uint<std::time_t>(b, result);
    if( ! result ) return;
    if( t != 0 )
        m_exptime = (t > EXPTIME_THRESHOLD) ? t :
            (g_stats.current_time.load(relaxed) + t);
    b = exptime_end;

    while( *b == SP ) ++b;
    if( b != e ) {
        // noreply ?
        const char* we = cfind(b, SP, e-b);
        std::size_t noreply_len = (we != nullptr) ? (we - b) : (e - b);
        if( noreply_len != 7 ) return;
        if( std::memcmp(b, "noreply", 7) != 0 ) return;
        m_no_reply = true;
        b = (we != nullptr) ? we : e;
    }

    while( *b == SP ) ++b;
    if( b != e ) return; // garbage left

    m_valid = true;
}

inline void
text_request::parse_lock(const char* b, const char* e) noexcept {
    while( *b == SP ) ++b;
    if( b == e ) return;
    const char* key_end = cfind(b, SP, e-b);
    if( key_end == nullptr ) key_end = e;
    m_key = item(b, key_end-b);
    b = key_end;

    while( *b == SP ) ++b;
    if( b != e ) return; // garbage left

    m_valid = true;
}

inline void
text_request::parse_unlock(const char* b, const char* e) noexcept {
    while( *b == SP ) ++b;
    if( b == e ) return;
    const char* key_end = cfind(b, SP, e-b);
    if( key_end == nullptr ) key_end = e;
    m_key = item(b, key_end-b);
    b = key_end;

    while( *b == SP ) ++b;
    if( b != e ) return; // garbage left

    m_valid = true;
}

inline void
text_request::parse_stats(const char* b, const char* e) noexcept {
    while( *b == SP ) ++b;
    if( b == e ) {
        m_valid = true;
        return;
    }

    const char* value_end = cfind(b, SP, e-b);
    if( value_end == nullptr ) value_end = e;
    std::size_t value_len = value_end - b;

    if( value_len == 3 && std::memcmp(b, "ops", 3) == 0 ) {
        m_stats = stats_t::OPS;
        m_valid = true;
        return;
    }
    if( value_len == 5 ) {
        if( std::memcmp(b, "items", 5) == 0 ) {
            m_stats = stats_t::ITEMS;
            m_valid = true;
            return;
        }
        if( std::memcmp(b, "sizes", 5) == 0 ) {
            m_stats = stats_t::SIZES;
            m_valid = true;
            return;
        }
    }
    if( value_len == 8 && std::memcmp(b, "settings", 8) == 0 ) {
        m_stats = stats_t::SETTINGS;
        m_valid = true;
        return;
    }
}

inline void
text_request::parse_incdec(const char* b, const char* e) noexcept {
    while( *b == SP ) ++b;
    if( b == e ) return;
    const char* key_end = cfind(b, SP, e-b);
    if( key_end == nullptr ) return;
    m_key = item(b, key_end-b);
    b = key_end;

    while( *b == SP ) ++b;
    if( b == e ) return;
    const char* value_end = cfind(b, SP, e-b);
    if( value_end == nullptr ) value_end = e;
    bool result;
    m_value = to_uint<std::uint64_t>(b, result);
    if( ! result ) return;
    b = value_end;

    while( *b == SP ) ++b;
    if( b != e ) {
        // noreply ?
        const char* we = cfind(b, SP, e-b);
        std::size_t noreply_len = (we != nullptr) ? (we - b) : (e - b);
        if( noreply_len != 7 ) return;
        if( std::memcmp(b, "noreply", 7) != 0 ) return;
        m_no_reply = true;
        b = (we != nullptr) ? we : e;
    }

    while( *b == SP ) ++b;
    if( b != e ) return; // garbage left

    m_valid = true;
}

inline void
text_request::parse_get(const char* b, const char* e) noexcept {
    while( *b == SP ) ++b;
    if( b == e ) return;
    const char* key_end = cfind(b, SP, e-b);
    if( key_end == nullptr ) key_end = e;
    m_key = item(b, key_end-b);
    m_valid = true;
}

void text_request::parse() noexcept {
    const char* eol = cfind(m_p, LF, m_len);
    if( eol == nullptr ) return; // incomplete

    m_request_len = eol - m_p + 1;
    if( m_request_len == 1 ) return;  // invalid
    if( *(--eol) != CR ) return;

    const char* cmd_start = m_p;
    while( *cmd_start == SP ) ++cmd_start;
    const char* cmd_end = cfind(cmd_start, SP, (eol - cmd_start));
    if( cmd_end == nullptr ) cmd_end = eol;
    std::size_t cmd_len = (cmd_end - cmd_start);

    if( cmd_len == 10 ) {
        if( std::memcmp(cmd_start, "unlock_all", 10) == 0 ) {
            m_command = text_command::UNLOCK_ALL;
            m_valid = true;
            return;
        }
    }
    if( cmd_len == 9 ) {
        if( std::memcmp(cmd_start, "flush_all", 9) == 0 ) {
            m_command = text_command::FLUSH_ALL;
            parse_flushall(cmd_end, eol);
            return;
        }
        if( std::memcmp(cmd_start, "verbosity", 9) == 0 ) {
            m_command = text_command::VERBOSITY;
            parse_verbosity(cmd_end, eol);
            return;
        }
    }
    if( cmd_len == 7 ) {
        if( std::memcmp(cmd_start, "version", 7) == 0 ) {
            m_command = text_command::VERSION;
            m_valid = true;
            return;
        }
        if( std::memcmp(cmd_start, "prepend", 7) == 0 ) {
            m_command = text_command::PREPEND;
            parse_storage(cmd_end, eol, false);
            return;
        }
        if( std::memcmp(cmd_start, "replace", 7) == 0 ) {
            m_command = text_command::REPLACE;
            parse_storage(cmd_end, eol, false);
            return;
        }
    }
    if( cmd_len == 6 ) {
        if( std::memcmp(cmd_start, "append", 6) == 0 ) {
            m_command = text_command::APPEND;
            parse_storage(cmd_end, eol, false);
            return;
        }
        if( std::memcmp(cmd_start, "delete", 6) == 0 ) {
            m_command = text_command::DELETE;
            parse_delete(cmd_end, eol);
            return;
        }
        if( std::memcmp(cmd_start, "unlock", 6) == 0 ) {
            m_command = text_command::UNLOCK;
            parse_unlock(cmd_end, eol);
            return;
        }
    }
    if( cmd_len == 5 ) {
        if( std::memcmp(cmd_start, "touch", 5) == 0 ) {
            m_command = text_command::TOUCH;
            parse_touch(cmd_end, eol);
            return;
        }
        if( std::memcmp(cmd_start, "slabs", 5) == 0 ) {
            m_command = text_command::SLABS;
            m_valid = true;
            // ignore.
            return;
        }
        if( std::memcmp(cmd_start, "stats", 5) == 0 ) {
            m_command = text_command::STATS;
            parse_stats(cmd_end, eol);
            return;
        }
    }
    if( cmd_len == 4 ) {
        if( std::memcmp(cmd_start, "gets", 4) == 0 ) {
            m_command = text_command::GETS;
            parse_get(cmd_end, eol);
            return;
        }
        if( std::memcmp(cmd_start, "incr", 4) == 0 ) {
            m_command = text_command::INCR;
            parse_incdec(cmd_end, eol);
            return;
        }
        if( std::memcmp(cmd_start, "decr", 4) == 0 ) {
            m_command = text_command::DECR;
            parse_incdec(cmd_end, eol);
            return;
        }
        if( std::memcmp(cmd_start, "lock", 4) == 0 ) {
            m_command = text_command::LOCK;
            parse_lock(cmd_end, eol);
            return;
        }
        if( std::memcmp(cmd_start, "quit", 4) == 0 ) {
            m_command = text_command::QUIT;
            m_valid = true;
            return;
        }
    }
    if( cmd_len == 3 ) {
        if( std::memcmp(cmd_start, "set", 3) == 0 ) {
            m_command = text_command::SET;
            parse_storage(cmd_end, eol, false);
            return;
        }
        if( std::memcmp(cmd_start, "add", 3) == 0 ) {
            m_command = text_command::ADD;
            parse_storage(cmd_end, eol, false);
            return;
        }
        if( std::memcmp(cmd_start, "get", 3) == 0 ) {
            m_command = text_command::GET;
            parse_get(cmd_end, eol);
            return;
        }
        if( std::memcmp(cmd_start, "cas", 3) == 0 ) {
            m_command = text_command::CAS;
            parse_storage(cmd_end, eol, true);
            return;
        }
    }
}

void text_response::value(const cybozu::hash_key& key, std::uint32_t flags,
                          const cybozu::dynbuf& data) {
    if( key.length() > MAX_KEY_LENGTH )
        throw std::logic_error("MAX_KEY_LENGTH over bug");
    m_iov[0] = {VALUE, sizeof(VALUE) - 1};
    m_iov[1] = {key.data(), key.length()};
    char buf[MAX_KEY_LENGTH + 100];
    static_assert( sizeof(unsigned int) >= sizeof(std::uint32_t),
                   "unsigned int is smaller than std::uint32_t" );
    int length = snprintf(buf, sizeof(buf), " %u %llu\x0d\x0a",
                          (unsigned int)flags,
                          (long long unsigned int)data.size());
    m_iov[2] = {buf, (std::size_t)length};
    m_iov[3] = {data.data(), data.size()};
    m_iov[4] = {CRLF, sizeof(CRLF) - 1};
    m_socket.sendv(m_iov, 5, false);
}

void text_response::value(const cybozu::hash_key& key, std::uint32_t flags,
                          const cybozu::dynbuf& data, std::uint64_t cas) {
    if( key.length() > MAX_KEY_LENGTH )
        throw std::logic_error("MAX_KEY_LENGTH over bug");
    m_iov[0] = {VALUE, sizeof(VALUE) - 1};
    m_iov[1] = {key.data(), key.length()};
    char buf[MAX_KEY_LENGTH + 100];
    static_assert( sizeof(unsigned int) >= sizeof(std::uint32_t),
                   "unsigned int is smaller than std::uint32_t" );
    int length = snprintf(buf, sizeof(buf), " %u %llu %llu\x0d\x0a",
                          (unsigned int)flags,
                          (long long unsigned int)data.size(),
                          (long long unsigned int)cas);
    m_iov[2] = {buf, (std::size_t)length};
    m_iov[3] = {data.data(), data.size()};
    m_iov[4] = {CRLF, sizeof(CRLF) - 1};
    m_socket.sendv(m_iov, 5, false);
}

void text_response::stats_settings() {
    std::ostringstream os;
    os << "STAT maxbytes " << g_config.memory_limit() << CRLF;
    os << "STAT tcpport " << g_config.port() << CRLF;
    os << "STAT replport " << g_config.repl_port() << CRLF;
    os << "STAT virtual_ip " << g_config.vip().str() << CRLF;
    os << "STAT evictions on" << CRLF;
    os << "STAT cas_enabled on" << CRLF;
    os << "STAT locking on" << CRLF;
    os << "STAT tmp_dir " << g_config.tempdir() << CRLF;
    os << "STAT buckets " << g_config.buckets() << CRLF;
    os << "STAT item_size_max " << g_config.max_data_size() << CRLF;
    os << "STAT num_threads " << g_config.workers() << CRLF;
    os << "STAT gc_interval " << g_config.gc_interval() << CRLF;
    std::string s = os.str();
    m_socket.send(s.data(), s.size());
}

void text_response::stats_items() {
    std::ostringstream os;
    os << "STAT items:1:number "
       << g_stats.objects.load(relaxed) << CRLF;
    os << "STAT items:1:age "
       << g_stats.oldest_age.load(relaxed) << CRLF;
    os << "STAT items:1:evicted "
       << g_stats.total_evictions.load(relaxed) << CRLF;
    os << "STAT items:1:conflicts "
       << g_stats.conflicts.load(relaxed) << CRLF;
    os << "STAT items:1:largest "
       << g_stats.largest_object_size.load(relaxed) << CRLF;
    std::string s = os.str();
    m_socket.send(s.data(), s.size());
}

void text_response::stats_sizes() {
    std::ostringstream os;
    os << "STAT 1024 "
       << g_stats.objects_under_1k.load(relaxed) << CRLF;
    os << "STAT 4096 "
       << g_stats.objects_under_4k.load(relaxed) << CRLF;
    os << "STAT 16384 "
       << g_stats.objects_under_16k.load(relaxed) << CRLF;
    os << "STAT 65536 "
       << g_stats.objects_under_64k.load(relaxed) << CRLF;
    os << "STAT 262144 "
       << g_stats.objects_under_256k.load(relaxed) << CRLF;
    os << "STAT 1048576 "
       << g_stats.objects_under_1m.load(relaxed) << CRLF;
    os << "STAT 4194304 "
       << g_stats.objects_under_4m.load(relaxed) << CRLF;
    os << "STAT huge "
       << g_stats.objects_huge.load(relaxed) << CRLF;
    std::string s = os.str();
    m_socket.send(s.data(), s.size());
}

void text_response::stats_ops() {
    std::ostringstream os;
#define SEND_TEXT_OPS(n,i)                                              \
    os << "STAT text:" n " "                                            \
       << g_stats.text_ops[(std::size_t)text_command::i].load(relaxed) << CRLF;
    SEND_TEXT_OPS("set", SET);
    SEND_TEXT_OPS("add", ADD);
    SEND_TEXT_OPS("replace", REPLACE);
    SEND_TEXT_OPS("append", APPEND);
    SEND_TEXT_OPS("prepend", PREPEND);
    SEND_TEXT_OPS("cas", CAS);
    SEND_TEXT_OPS("get", GET);
    SEND_TEXT_OPS("gets", GETS);
    SEND_TEXT_OPS("delete", DELETE);
    SEND_TEXT_OPS("incr", INCR);
    SEND_TEXT_OPS("decr", DECR);
    SEND_TEXT_OPS("touch", TOUCH);
    SEND_TEXT_OPS("lock", LOCK);
    SEND_TEXT_OPS("unlock", UNLOCK);
    SEND_TEXT_OPS("unlock_all", UNLOCK_ALL);
    SEND_TEXT_OPS("slabs", SLABS);
    SEND_TEXT_OPS("stats", STATS);
    SEND_TEXT_OPS("flush_all", FLUSH_ALL);
    SEND_TEXT_OPS("version", VERSION);
    SEND_TEXT_OPS("verbosity", VERBOSITY);
    SEND_TEXT_OPS("quit", QUIT);
#undef SEND_TEXT_OPS

#define SEND_BINARY_OPS(n)                                              \
    os << "STAT binary:" #n " "                                         \
       << g_stats.bin_ops[(std::size_t)binary_command::n].load(relaxed) << CRLF;
    SEND_BINARY_OPS(Get);
    SEND_BINARY_OPS(Set);
    SEND_BINARY_OPS(Add);
    SEND_BINARY_OPS(Replace);
    SEND_BINARY_OPS(Delete);
    SEND_BINARY_OPS(Increment);
    SEND_BINARY_OPS(Decrement);
    SEND_BINARY_OPS(Quit);
    SEND_BINARY_OPS(Flush);
    SEND_BINARY_OPS(GetQ);
    SEND_BINARY_OPS(Noop);
    SEND_BINARY_OPS(Version);
    SEND_BINARY_OPS(GetK);
    SEND_BINARY_OPS(GetKQ);
    SEND_BINARY_OPS(Append);
    SEND_BINARY_OPS(Prepend);
    SEND_BINARY_OPS(Stat);
    SEND_BINARY_OPS(SetQ);
    SEND_BINARY_OPS(AddQ);
    SEND_BINARY_OPS(ReplaceQ);
    SEND_BINARY_OPS(DeleteQ);
    SEND_BINARY_OPS(IncrementQ);
    SEND_BINARY_OPS(DecrementQ);
    SEND_BINARY_OPS(QuitQ);
    SEND_BINARY_OPS(FlushQ);
    SEND_BINARY_OPS(AppendQ);
    SEND_BINARY_OPS(PrependQ);
    SEND_BINARY_OPS(Touch);
    SEND_BINARY_OPS(GaT);
    SEND_BINARY_OPS(GaTQ);
    SEND_BINARY_OPS(GaTK);
    SEND_BINARY_OPS(GaTKQ);
    SEND_BINARY_OPS(Lock);
    SEND_BINARY_OPS(LockQ);
    SEND_BINARY_OPS(Unlock);
    SEND_BINARY_OPS(UnlockQ);
    SEND_BINARY_OPS(UnlockAll);
    SEND_BINARY_OPS(UnlockAllQ);
    SEND_BINARY_OPS(LaG);
    SEND_BINARY_OPS(LaGQ);
    SEND_BINARY_OPS(LaGK);
    SEND_BINARY_OPS(LaGKQ);
    SEND_BINARY_OPS(RaU);
    SEND_BINARY_OPS(RaUQ);
#undef SEND_BINARY_OPS

    std::string s = os.str();
    m_socket.send(s.data(), s.size());
}

void text_response::stats_general(std::size_t n_slaves) {
    struct rusage ru;
    if( ::getrusage(RUSAGE_SELF, &ru) == -1 )
        cybozu::throw_unix_error(errno, "getrusage");

    std::ostringstream os;
    os << "STAT pid " << ::getpid() << CRLF;
    os << "STAT time " << g_stats.current_time.load(relaxed) << CRLF;
    os << "STAT version " << VERSION << CRLF;
    os << "STAT pointer_size " << sizeof(void*)*8 << CRLF;
    os << "STAT rusage_user " << ru.ru_utime.tv_sec << ":"
       << ru.ru_utime.tv_usec << CRLF;
    os << "STAT rusage_system " << ru.ru_stime.tv_sec << ":"
       << ru.ru_stime.tv_usec << CRLF;
    os << "STAT curr_connections " << g_stats.curr_connections.load(relaxed) << CRLF;
    os << "STAT total_connections " << g_stats.total_connections.load(relaxed) << CRLF;
    os << "STAT curr_items " << g_stats.objects.load(relaxed) << CRLF;
    os << "STAT total_items " << g_stats.total_objects.load(relaxed) << CRLF;
    os << "STAT bytes " << g_stats.used_memory.load(relaxed) << CRLF;
    os << "STAT limit_maxbytes " << g_config.memory_limit() << CRLF;
    os << "STAT threads " << g_config.workers() << CRLF;
    os << "STAT gc_count " << g_stats.gc_count.load(relaxed) << CRLF;
    os << "STAT slaves " << n_slaves << CRLF;
    os << "STAT last_expirations "
       << g_stats.last_expirations.load(relaxed) << CRLF;
    os << "STAT last_evictions "
       << g_stats.last_evictions.load(relaxed) << CRLF;
    os << "STAT evictions "
       << g_stats.total_evictions.load(relaxed) << CRLF;
    os << "STAT last_gc_elapsed "
       << g_stats.last_gc_elapsed.load(relaxed) << CRLF;
    os << "STAT total_gc_elapsed "
       << g_stats.total_gc_elapsed.load(relaxed) << CRLF;
    std::string s = os.str();
    m_socket.send(s.data(), s.size());
}

void text_response::version() {
    m_iov[0] = {TEXT_VERSION, sizeof(TEXT_VERSION) -1};
    m_iov[1] = {VERSION, sizeof(VERSION) - 1};
    m_iov[2] = {CRLF, sizeof(CRLF) - 1};
    m_socket.sendv(m_iov, 3, true);
}

/*
 * Binary protocol.
 */

void binary_request::parse() noexcept {
    if( m_len < BINARY_HEADER_SIZE ) return; // incomplete
    std::uint32_t total_len;
    cybozu::ntoh(m_p + 8, total_len);
    if( m_len < (BINARY_HEADER_SIZE + total_len) ) return; // incomplete
    m_request_len = BINARY_HEADER_SIZE + total_len;

    // Opcode parsing
    m_command = (binary_command)*(unsigned char*)(m_p+1);
    m_quiet = ( m_command == binary_command::GetQ ||
                m_command == binary_command::GetKQ ||
                (binary_command::SetQ <= m_command &&
                 m_command <= binary_command::PrependQ) ||
                m_command == binary_command::GaTQ ||
                m_command == binary_command::GaTKQ ||
                (binary_command::LockQ <= m_command &&
                 m_command <= binary_command::RaUQ &&
                 ((unsigned char)m_command) & 1) );

    std::uint16_t key_len;
    cybozu::ntoh(m_p + 2, key_len);
    if( key_len > MAX_KEY_LENGTH )
        return; // invalid
    std::uint8_t extras_len = *(unsigned char*)(m_p + 4);
    if( total_len < (key_len + extras_len) )
        return; // invalid
    if( key_len > 0 )
        m_key = item(m_p + (BINARY_HEADER_SIZE + extras_len), key_len);

    cybozu::ntoh(m_p + 16, m_cas_unique);

    std::size_t data_len = total_len - key_len - extras_len;
    if( data_len > MAX_REQUEST_LENGTH ) {
        m_status = binary_status::TooLargeValue;
        return;
    }
    if( data_len > 0 )
        m_data = item(m_p + (BINARY_HEADER_SIZE + extras_len + key_len),
                      data_len);

    const char* const p_extra = m_p + BINARY_HEADER_SIZE;

    switch( m_command ) {
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
        if( extras_len != 0 && extras_len != 4 )
            return; // invalid
        if( key_len == 0 || data_len > 0 )
            return; // invalid
        if( extras_len == 4 ) {
            m_exptime = binary_exptime(p_extra);
        } else {
            m_exptime = EXPTIME_NONE;
        }
        break;
    case binary_command::Set:
    case binary_command::SetQ:
    case binary_command::Add:
    case binary_command::AddQ:
    case binary_command::Replace:
    case binary_command::ReplaceQ:
    case binary_command::RaU:
    case binary_command::RaUQ:
        if( extras_len != 8 || key_len == 0 || data_len == 0 )
            return; // invalid
        cybozu::ntoh(p_extra, m_flags);
        m_exptime = binary_exptime(p_extra + 4);
        break;
    case binary_command::Delete:
    case binary_command::DeleteQ:
        if( extras_len != 0 || key_len == 0 || data_len > 0 )
            return; // invalid
        break;
    case binary_command::Increment:
    case binary_command::IncrementQ:
    case binary_command::Decrement:
    case binary_command::DecrementQ:
        if( extras_len != 20 || key_len == 0 || data_len > 0 )
            return; // invalid
        cybozu::ntoh(p_extra, m_value);
        cybozu::ntoh(p_extra + sizeof(std::uint64_t), m_initial);
        m_exptime = binary_exptime(p_extra + sizeof(std::uint64_t)*2);
        break;
    case binary_command::Touch:
        if( key_len == 0 || extras_len != 4 || data_len != 0 )
            return; // invalid
        m_exptime = binary_exptime(p_extra);
        break;
    case binary_command::Flush:
    case binary_command::FlushQ:
        if( extras_len != 0 && extras_len != 4 )
            return; // invalid
        if( key_len != 0 || data_len > 0 )
            return; // invalid
        if( extras_len == 4 ) { // yrmcds extension
            m_exptime = binary_exptime(p_extra);
        } else {
            m_exptime = g_stats.current_time.load(relaxed);
        }
        break;
    case binary_command::Append:
    case binary_command::AppendQ:
    case binary_command::Prepend:
    case binary_command::PrependQ:
        if( extras_len != 0 || key_len == 0 || data_len == 0 )
            return; // invalid
        break;
    case binary_command::Lock:
    case binary_command::LockQ:
    case binary_command::Unlock:
    case binary_command::UnlockQ:
        if( extras_len != 0 || key_len == 0 || data_len > 0 )
            return; // invalid
        break;
    case binary_command::Stat:
        if( extras_len != 0 || data_len != 0 )
            return; // invalid
        if( key_len == 8 &&
            std::memcmp(p_extra, "settings", 8) == 0 ) {
            m_stats = stats_t::SETTINGS;
        } else if( key_len == 5 ) {
            if( std::memcmp(p_extra, "items", 5) == 0 ) {
                m_stats = stats_t::ITEMS;
            } else if( std::memcmp(p_extra, "sizes", 5) == 0 ) {
                m_stats = stats_t::SIZES;
            }
        } else if( key_len == 3 &&
                   std::memcmp(p_extra, "ops", 3) == 0 ) {
            m_stats = stats_t::OPS;
        }
        break;
    case binary_command::Quit:
    case binary_command::QuitQ:
    case binary_command::Version:
    case binary_command::Noop:
    case binary_command::UnlockAll:
    case binary_command::UnlockAllQ:
        // no validation.
        // this is intentional violation against the protocol definition.
        break;
    default:
        m_command = binary_command::Unknown;
        m_status = binary_status::UnknownCommand;
        return;
    }

    m_status = binary_status::OK;
}

inline void
binary_response::send_error(binary_status status,
                            const char* msg, std::size_t len) {
    if( len > 100 )
        throw std::logic_error("<memcache::binary_response::send_error> Too long message");
    char header[BINARY_HEADER_SIZE + 100];
    fill_header(header, 0, 0, len, 0, status);
    std::memcpy(&header[BINARY_HEADER_SIZE], msg, len);
    m_socket.send(header, BINARY_HEADER_SIZE + len, true);
}

void
binary_response::error(binary_status status) {
    switch( status ) {
    case binary_status::NotFound:
        send_error(status, STATUS_NOT_FOUND, sizeof(STATUS_NOT_FOUND) - 1);
        break;
    case binary_status::Exists:
        send_error(status, STATUS_EXISTS, sizeof(STATUS_EXISTS) - 1);
        break;
    case binary_status::TooLargeValue:
        send_error(status, STATUS_TOO_LARGE, sizeof(STATUS_TOO_LARGE) - 1);
        break;
    case binary_status::Invalid:
        send_error(status, STATUS_INVALID, sizeof(STATUS_INVALID) - 1);
        break;
    case binary_status::NotStored:
        send_error(status, STATUS_NOT_STORED, sizeof(STATUS_NOT_STORED) - 1);
        break;
    case binary_status::NonNumeric:
        send_error(status, STATUS_NON_NUMERIC, sizeof(STATUS_NON_NUMERIC) - 1);
        break;
    case binary_status::Locked:
        send_error(status, STATUS_LOCKED, sizeof(STATUS_LOCKED) - 1);
        break;
    case binary_status::NotLocked:
        send_error(status, STATUS_NOT_LOCKED, sizeof(STATUS_NOT_LOCKED) - 1);
        break;
    case binary_status::UnknownCommand:
        send_error(status, STATUS_UNKNOWN, sizeof(STATUS_UNKNOWN) - 1);
        break;
    case binary_status::OutOfMemory:
        send_error(status, STATUS_OOM, sizeof(STATUS_OOM) - 1);
        break;
    default:
        throw std::logic_error("<memcache::binary_response::error> bug");
    }
}

void
binary_response::success() {
    char header[BINARY_HEADER_SIZE];
    fill_header(header, 0, 0, 0, 0);
    m_socket.send(header, BINARY_HEADER_SIZE, true);
}

void
binary_response::get(std::uint32_t flags, const cybozu::dynbuf& data,
                     std::uint64_t cas, bool flush,
                     const char* key, std::size_t key_len) {
    char header[BINARY_HEADER_SIZE];
    fill_header(header, key_len, sizeof(flags), data.size(), cas);
    char b_flags[sizeof(flags)];
    cybozu::hton(flags, b_flags);
    m_iov[0] = {header, BINARY_HEADER_SIZE};
    m_iov[1] = {b_flags, sizeof(b_flags)};
    if( key == nullptr ) {
        m_iov[2] = {data.data(), data.size()};
        m_socket.sendv(m_iov, 3, flush);
    } else {
        m_iov[2] = {key, key_len};
        m_iov[3] = {data.data(), data.size()};
        m_socket.sendv(m_iov, 4, flush);
    }
}

void
binary_response::set(std::uint64_t cas) {
    char header[BINARY_HEADER_SIZE];
    fill_header(header, 0, 0, 0, cas);
    m_socket.send(header, BINARY_HEADER_SIZE, true);
}

void
binary_response::incdec(std::uint64_t value, std::uint64_t cas) {
    char header[BINARY_HEADER_SIZE + sizeof(value)];
    fill_header(header, 0, 0, sizeof(value), cas);
    cybozu::hton(value, &header[BINARY_HEADER_SIZE]);
    m_socket.send(header, sizeof(header), true);
}

void
binary_response::quit() {
    char header[BINARY_HEADER_SIZE];
    fill_header(header, 0, 0, 0, 0);
    m_socket.send_close(header, sizeof(header));
}

inline void
binary_response::send_stat(const std::string& key, const std::string& value) {
    char header[BINARY_HEADER_SIZE];
    fill_header(header, key.size(), 0, value.size(), 0);
    m_iov[0] = {header, sizeof(header)};
    m_iov[1] = {key.data(), key.size()};
    m_iov[2] = {value.data(), value.size()};
    m_socket.sendv(m_iov, 3, false);
}

void
binary_response::stats_settings() {
    send_stat("maxbytes", std::to_string(g_config.memory_limit()));
    send_stat("tcpport", std::to_string(g_config.port()));
    send_stat("replport", std::to_string(g_config.repl_port()));
    send_stat("virtual_ip", g_config.vip().str());
    send_stat("evictions", "on");
    send_stat("cas_enabled", "on");
    send_stat("locking", "on");
    send_stat("tmp_dir", g_config.tempdir());
    send_stat("buckets", std::to_string(g_config.buckets()));
    send_stat("item_size_max", std::to_string(g_config.max_data_size()));
    send_stat("num_threads", std::to_string(g_config.workers()));
    send_stat("gc_interval", std::to_string(g_config.gc_interval()));
    success();
}

void
binary_response::stats_items() {
    send_stat("items:1:number",
              std::to_string(g_stats.objects.load(relaxed)));
    send_stat("items:1:age",
              std::to_string(g_stats.oldest_age.load(relaxed)));
    send_stat("items:1:evicted",
              std::to_string(g_stats.total_evictions.load(relaxed)));
    send_stat("items:1:conflicts",
              std::to_string(g_stats.conflicts.load(relaxed)));
    send_stat("items:1:largest",
              std::to_string(g_stats.largest_object_size.load(relaxed)));
    success();
}

void
binary_response::stats_sizes() {
    send_stat("1024",
              std::to_string(g_stats.objects_under_1k.load(relaxed)));
    send_stat("4096",
              std::to_string(g_stats.objects_under_4k.load(relaxed)));
    send_stat("16384",
              std::to_string(g_stats.objects_under_16k.load(relaxed)));
    send_stat("65536",
              std::to_string(g_stats.objects_under_64k.load(relaxed)));
    send_stat("262144",
              std::to_string(g_stats.objects_under_256k.load(relaxed)));
    send_stat("1048576",
              std::to_string(g_stats.objects_under_1m.load(relaxed)));
    send_stat("4194304",
              std::to_string(g_stats.objects_under_4m.load(relaxed)));
    send_stat("huge",
              std::to_string(g_stats.objects_huge.load(relaxed)));
    success();
}

void
binary_response::stats_ops() {
#define SEND_TEXT_OPS(n,i)                                              \
    send_stat("text:" n,                                                \
              std::to_string(g_stats.text_ops[(std::size_t)text_command::i].load(relaxed)))
    SEND_TEXT_OPS("set", SET);
    SEND_TEXT_OPS("add", ADD);
    SEND_TEXT_OPS("replace", REPLACE);
    SEND_TEXT_OPS("append", APPEND);
    SEND_TEXT_OPS("prepend", PREPEND);
    SEND_TEXT_OPS("cas", CAS);
    SEND_TEXT_OPS("get", GET);
    SEND_TEXT_OPS("gets", GETS);
    SEND_TEXT_OPS("delete", DELETE);
    SEND_TEXT_OPS("incr", INCR);
    SEND_TEXT_OPS("decr", DECR);
    SEND_TEXT_OPS("touch", TOUCH);
    SEND_TEXT_OPS("lock", LOCK);
    SEND_TEXT_OPS("unlock", UNLOCK);
    SEND_TEXT_OPS("unlock_all", UNLOCK_ALL);
    SEND_TEXT_OPS("slabs", SLABS);
    SEND_TEXT_OPS("stats", STATS);
    SEND_TEXT_OPS("flush_all", FLUSH_ALL);
    SEND_TEXT_OPS("version", VERSION);
    SEND_TEXT_OPS("verbosity", VERBOSITY);
    SEND_TEXT_OPS("quit", QUIT);
#undef SEND_TEXT_OPS

#define SEND_BINARY_OPS(n)                                              \
    send_stat("binary:" #n,                                             \
              std::to_string(g_stats.bin_ops[(std::size_t)binary_command::n].load(relaxed)))
    SEND_BINARY_OPS(Get);
    SEND_BINARY_OPS(Set);
    SEND_BINARY_OPS(Add);
    SEND_BINARY_OPS(Replace);
    SEND_BINARY_OPS(Delete);
    SEND_BINARY_OPS(Increment);
    SEND_BINARY_OPS(Decrement);
    SEND_BINARY_OPS(Quit);
    SEND_BINARY_OPS(Flush);
    SEND_BINARY_OPS(GetQ);
    SEND_BINARY_OPS(Noop);
    SEND_BINARY_OPS(Version);
    SEND_BINARY_OPS(GetK);
    SEND_BINARY_OPS(GetKQ);
    SEND_BINARY_OPS(Append);
    SEND_BINARY_OPS(Prepend);
    SEND_BINARY_OPS(Stat);
    SEND_BINARY_OPS(SetQ);
    SEND_BINARY_OPS(AddQ);
    SEND_BINARY_OPS(ReplaceQ);
    SEND_BINARY_OPS(DeleteQ);
    SEND_BINARY_OPS(IncrementQ);
    SEND_BINARY_OPS(DecrementQ);
    SEND_BINARY_OPS(QuitQ);
    SEND_BINARY_OPS(FlushQ);
    SEND_BINARY_OPS(AppendQ);
    SEND_BINARY_OPS(PrependQ);
    SEND_BINARY_OPS(Touch);
    SEND_BINARY_OPS(GaT);
    SEND_BINARY_OPS(GaTQ);
    SEND_BINARY_OPS(GaTK);
    SEND_BINARY_OPS(GaTKQ);
    SEND_BINARY_OPS(Lock);
    SEND_BINARY_OPS(LockQ);
    SEND_BINARY_OPS(Unlock);
    SEND_BINARY_OPS(UnlockQ);
    SEND_BINARY_OPS(UnlockAll);
    SEND_BINARY_OPS(UnlockAllQ);
    SEND_BINARY_OPS(LaG);
    SEND_BINARY_OPS(LaGQ);
    SEND_BINARY_OPS(LaGK);
    SEND_BINARY_OPS(LaGKQ);
    SEND_BINARY_OPS(RaU);
    SEND_BINARY_OPS(RaUQ);
#undef SEND_BINARY_OPS
    success();
}

void
binary_response::stats_general(std::size_t n_slaves) {
    struct rusage ru;
    if( ::getrusage(RUSAGE_SELF, &ru) == -1 )
        cybozu::throw_unix_error(errno, "getrusage");

    send_stat("pid", std::to_string(::getpid()));
    send_stat("time", std::to_string(g_stats.current_time.load(relaxed)));
    send_stat("version", VERSION);
    send_stat("pointer_size", std::to_string(sizeof(void*)*8));
    send_stat("rusage_user",
              std::to_string(ru.ru_utime.tv_sec) + ":" +
              std::to_string(ru.ru_utime.tv_usec));
    send_stat("rusage_system",
              std::to_string(ru.ru_stime.tv_sec) + ":" +
              std::to_string(ru.ru_stime.tv_usec));
    send_stat("curr_connections",
              std::to_string(g_stats.curr_connections.load(relaxed)));
    send_stat("total_connections",
              std::to_string(g_stats.total_connections.load(relaxed)));
    send_stat("curr_items", std::to_string(g_stats.objects.load(relaxed)));
    send_stat("total_items",
              std::to_string(g_stats.total_objects.load(relaxed)));
    send_stat("bytes", std::to_string(g_stats.used_memory.load(relaxed)));
    send_stat("limit_maxbytes", std::to_string(g_config.memory_limit()));
    send_stat("threads", std::to_string(g_config.workers()));
    send_stat("gc_count", std::to_string(g_stats.gc_count.load(relaxed)));
    send_stat("slaves", std::to_string(n_slaves));
    send_stat("last_expirations",
              std::to_string(g_stats.last_expirations.load(relaxed)));
    send_stat("last_evictions",
              std::to_string(g_stats.last_evictions.load(relaxed)));
    send_stat("evictions",
              std::to_string(g_stats.total_evictions.load(relaxed)));
    send_stat("last_gc_elapsed",
              std::to_string(g_stats.last_gc_elapsed.load(relaxed)));
    send_stat("total_gc_elapsed",
              std::to_string(g_stats.total_gc_elapsed.load(relaxed)));
    success();
}

void
binary_response::version() {
    char header[BINARY_HEADER_SIZE + sizeof(VERSION) - 1];
    fill_header(header, 0, 0, sizeof(VERSION) - 1, 0);
    std::memcpy(&header[BINARY_HEADER_SIZE], VERSION, sizeof(VERSION) - 1);
    m_socket.send(header, sizeof(header), true);
}

}} // namespace yrmcds::memcache
