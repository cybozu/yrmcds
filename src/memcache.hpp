// memcached server-side protocol.
// (C) 2013 Cybozu.

#ifndef YRMCDS_MEMCACHE_HPP
#define YRMCDS_MEMCACHE_HPP

#include "constants.hpp"

#include <cybozu/dynbuf.hpp>
#include <cybozu/hash_map.hpp>
#include <cybozu/logger.hpp>
#include <cybozu/tcp.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <stdexcept>
#include <tuple>

namespace yrmcds { namespace memcache {

inline bool is_binary_request(const char* p) {
    return *p == '\x80';
}

// Possible stats categories.
enum class stats_t {
    GENERAL, SETTINGS, ITEMS, SIZES, OPS
};

using item = std::tuple<const char*, std::size_t>;


// Memcache text commands.
enum class text_command {
    UNKNOWN, SET, ADD, REPLACE, APPEND, PREPEND, CAS, GET, GETS, DELETE,
    INCR, DECR, TOUCH, LOCK, UNLOCK, UNLOCK_ALL, SLABS, STATS, FLUSH_ALL,
    VERSION, VERBOSITY, QUIT,
    END_OF_COMMAND // must be defined the last
};


// Text request parser.
class text_request final {
public:
    text_request(const char* p, std::size_t len):
        m_p(p), m_len(len)
    {
        if( len == 0 || p == nullptr )
            throw std::logic_error("<text_request> bad ctor arguments");
        parse();
    }

    // End of the key stream.
    static constexpr item eos{};

    // Return length of the request.
    //
    // Return length of the request.
    // If the request is incomplete, zero is returned.
    std::size_t length() const noexcept { return m_request_len; }

    // Return the command type.
    text_command command() const noexcept { return m_command; }

    // Return `true` if the request can be parsed successfully.
    bool valid() const noexcept { return m_valid; }

    // `true` if the command has "noreply" option.
    bool no_reply() const noexcept { return m_no_reply; }

    // Return `key` sent with various commands.
    item key() const noexcept { return m_key; }

    // Return `flags` sent with storage commands.
    std::uint32_t flags() const noexcept { return m_flags; }

    // Return `exptime` sent with storage commands and TOUCH and FLUSH_ALL.
    std::time_t exptime() const noexcept { return m_exptime; }

    // Return `cas unique` sent with CAS command.
    std::uint64_t cas_unique() const noexcept { return m_cas_unique; }

    // Return data block sent with storage commands.
    item data() const noexcept { return m_data; }

    // Return `item` for the first key sent with GET or GETS.
    item first_key() const noexcept { return m_key; }

    // Return `item` next to the `prev` key in GET or GETS commands.
    // If the item == <text_request::eos>, no more key is available.
    static item next_key(const item& prev) noexcept {
        const char* b;
        std::size_t len;
        std::tie(b, len) = prev;

        b += len;
        while( *b == '\x20' ) ++b;
        const char* e = (const char*)::rawmemchr(b, '\x0d');
        if( b == e ) return eos;

        const char* key_end = (const char*)std::memchr(b, '\x20', e-b);
        if( key_end == nullptr ) key_end = e;
        return item(b, key_end-b);
    }

    // Return an unsigned 64bit integer value sent with INCR or DECR.
    std::uint64_t value() const noexcept { return m_value; }

    // Return the log verbosity sent with VERBOSITY.
    cybozu::severity verbosity() const noexcept { return m_verbosity; }

    // Return memcache stats category sent with STATS.
    stats_t stats() const noexcept { return m_stats; }

private:
    const char* const m_p;
    const std::size_t m_len;
    std::size_t m_request_len = 0;
    text_command m_command = text_command::UNKNOWN;
    bool m_valid = false;
    bool m_no_reply = false;
    item m_key;
    std::uint32_t m_flags = 0;
    std::time_t m_exptime = 0;
    std::uint64_t m_cas_unique = 0;
    item m_data;
    std::uint64_t m_value = 0;
    cybozu::severity m_verbosity = cybozu::severity::info;
    stats_t m_stats = stats_t::GENERAL;

    void parse() noexcept;
    void parse_flushall(const char* b, const char* e) noexcept;
    void parse_verbosity(const char* b, const char* e) noexcept;
    void parse_storage(const char* b, const char* e, bool is_cas) noexcept;
    void parse_delete(const char* b, const char* e) noexcept;
    void parse_touch(const char* b, const char* e) noexcept;
    void parse_lock(const char* b, const char* e) noexcept;
    void parse_unlock(const char* b, const char* e) noexcept;
    void parse_stats(const char* b, const char* e) noexcept;
    void parse_incdec(const char* b, const char* e) noexcept;
    void parse_get(const char* b, const char* e) noexcept;
};


const char TEXT_ERROR[] = "ERROR\x0d\x0a";
const char TEXT_OK[] = "OK\x0d\x0a";
const char TEXT_STORED[] = "STORED\x0d\x0a";
const char TEXT_NOT_STORED[] = "NOT_STORED\x0d\x0a";
const char TEXT_EXISTS[] = "EXISTS\x0d\x0a";
const char TEXT_NOT_FOUND[] = "NOT_FOUND\x0d\x0a";
const char TEXT_TOUCHED[] = "TOUCHED\x0d\x0a";
const char TEXT_DELETED[] = "DELETED\x0d\x0a";
const char TEXT_END[] = "END\x0d\x0a";
const char TEXT_LOCKED[] = "LOCKED\x0d\x0a";
const char TEXT_VERSION[] = "VERSION ";

// Text response sender.
class text_response final {
public:
    text_response(cybozu::tcp_socket& sock):
        m_socket(sock) {}

    void error() {
        m_socket.send(TEXT_ERROR, sizeof(TEXT_ERROR) - 1, true);
    }
    void ok() {
        m_socket.send(TEXT_OK, sizeof(TEXT_OK) - 1, true);
    }
    void end() {
        m_socket.send(TEXT_END, sizeof(TEXT_END) - 1, true);
    }
    void stored() {
        m_socket.send(TEXT_STORED, sizeof(TEXT_STORED) - 1, true);
    }
    void not_stored() {
        m_socket.send(TEXT_NOT_STORED, sizeof(TEXT_NOT_STORED) - 1, true);
    }
    void exists() {
        m_socket.send(TEXT_EXISTS, sizeof(TEXT_EXISTS) - 1, true);
    }
    void touched() {
        m_socket.send(TEXT_TOUCHED, sizeof(TEXT_TOUCHED) - 1, true);
    }
    void deleted() {
        m_socket.send(TEXT_DELETED, sizeof(TEXT_DELETED) - 1, true);
    }
    void not_found() {
        m_socket.send(TEXT_NOT_FOUND, sizeof(TEXT_NOT_FOUND) - 1, true);
    }
    void locked() {
        m_socket.send(TEXT_LOCKED, sizeof(TEXT_LOCKED) - 1, true);
    }

    void value(const cybozu::hash_key& key, std::uint32_t flags,
               const cybozu::dynbuf& data);
    void value(const cybozu::hash_key& key, std::uint32_t flags,
               const cybozu::dynbuf& data, std::uint64_t cas);

    void send(const char* p, std::size_t len, bool flush) {
        m_socket.send(p, len, flush);
    }

    void stats_settings();
    void stats_items();
    void stats_sizes();
    void stats_ops();
    void stats_general(std::size_t n_slaves);
    void version();

private:
    cybozu::tcp_socket& m_socket;
    cybozu::tcp_socket::iovec m_iov[cybozu::tcp_socket::MAX_IOVCNT];
};


// Memcache binary commands.
enum class binary_command: unsigned char {
    Get        = '\x00',
    Set        = '\x01',
    Add        = '\x02',
    Replace    = '\x03',
    Delete     = '\x04',
    Increment  = '\x05',
    Decrement  = '\x06',
    Quit       = '\x07',
    Flush      = '\x08',
    GetQ       = '\x09',
    Noop       = '\x0a',
    Version    = '\x0b',
    GetK       = '\x0c',
    GetKQ      = '\x0d',
    Append     = '\x0e',
    Prepend    = '\x0f',
    Stat       = '\x10',
    SetQ       = '\x11',
    AddQ       = '\x12',
    ReplaceQ   = '\x13',
    DeleteQ    = '\x14',
    IncrementQ = '\x15',
    DecrementQ = '\x16',
    QuitQ      = '\x17',
    FlushQ     = '\x18',
    AppendQ    = '\x19',
    PrependQ   = '\x1a',
    Touch      = '\x1c',
    GaT        = '\x1d',
    GaTQ       = '\x1e',
    GaTK       = '\x23',
    GaTKQ      = '\x24',

    Lock       = '\x40',
    LockQ      = '\x41',
    Unlock     = '\x42',
    UnlockQ    = '\x43',
    UnlockAll  = '\x44',
    UnlockAllQ = '\x45',
    LaG        = '\x46',
    LaGQ       = '\x47',
    LaGK       = '\x48',
    LaGKQ      = '\x49',
    RaU        = '\x4a',
    RaUQ       = '\x4b',

    Unknown,
    END_OF_COMMAND // must be defined the last
};


// Binary protocol response status
enum class binary_status: std::uint16_t {
    OK = 0x0000,
    NotFound = 0x0001,
    Exists = 0x0002,
    TooLargeValue = 0x0003,
    Invalid = 0x0004,
    NotStored = 0x0005,
    NonNumeric = 0x0006,
    Locked = 0x0010,
    NotLocked = 0x0011,
    UnknownCommand = 0x0081,
    OutOfMemory = 0x0082
};


// Binary request parser
class binary_request final {
public:
    binary_request(const char* p, std::size_t len):
        m_p(p), m_len(len)
    {
        if( len == 0 || p == nullptr )
            throw std::logic_error("<binary_request> bad ctor arguments");
        parse();
    }

    // Return length of the request.
    //
    // Return length of the request.
    // If the request is incomplete, zero is returned.
    std::size_t length() const noexcept { return m_request_len; }

    // Response status, if determined by the request.
    binary_status status() const noexcept { return m_status; }

    // Return the command type.
    binary_command command() const noexcept { return m_command; }

    // The command is quiet or not.
    bool quiet() const noexcept { return m_quiet; }

    // Return `key`.
    item key() const noexcept { return m_key; }

    // Return `opaque` sent with the request.
    const char* opaque() const noexcept { return m_p + 12; }

    // Return `cas unique` sent with CAS command.
    std::uint64_t cas_unique() const noexcept { return m_cas_unique; }

    // Return `flags` sent with storage commands.
    std::uint32_t flags() const noexcept { return m_flags; }

    // Special expiration time for increment/decrement.
    static const std::time_t EXPTIME_NONE = (std::time_t)-1;

    // Return `exptime`.
    std::time_t exptime() const noexcept { return m_exptime; }

    // Return data block sent with storage commands.
    item data() const noexcept { return m_data; }

    // Return an unsigned 64bit integer value for increment or decrement.
    std::uint64_t value() const noexcept { return m_value; }

    // Return an unsigned 64bit integer value to initialize an object.
    std::uint64_t initial() const noexcept { return m_initial; }

    // Return memcache stats category sent with STATS.
    stats_t stats() const noexcept { return m_stats; }

private:
    const char* const m_p;
    const std::size_t m_len;
    std::size_t m_request_len = 0;
    binary_status m_status = binary_status::Invalid;
    binary_command m_command;
    bool m_quiet;
    item m_key;
    std::uint64_t m_cas_unique;
    std::uint32_t m_flags = 0;
    std::time_t m_exptime = 0;
    item m_data;
    std::uint64_t m_value = 0;
    std::uint64_t m_initial = 0;
    stats_t m_stats = stats_t::GENERAL;

    void parse() noexcept;
};


// Binary response sender.
class binary_response final {
public:
    binary_response(cybozu::tcp_socket& sock, const binary_request& req):
        m_socket(sock), m_request(req) {}

    void error(binary_status status);
    void success();
    void get(std::uint32_t flags, const cybozu::dynbuf& data,
             std::uint64_t cas, bool flush,
             const char* key = nullptr, std::size_t key_len = 0);
    void set(std::uint64_t cas);
    void incdec(std::uint64_t value, std::uint64_t cas);
    void quit();
    void stats_settings();
    void stats_items();
    void stats_sizes();
    void stats_ops();
    void stats_general(std::size_t n_slaves);
    void version();

private:
    cybozu::tcp_socket& m_socket;
    const binary_request& m_request;
    cybozu::tcp_socket::iovec m_iov[cybozu::tcp_socket::MAX_IOVCNT];

    void send_error(binary_status status, const char* msg, std::size_t len);
    void send_stat(const std::string& key, const std::string& value);

    void fill_header(char* buf, std::uint16_t key_len, std::uint8_t extras_len,
                     std::uint32_t data_len, std::uint64_t cas,
                     binary_status status = binary_status::OK) const noexcept {
        buf[0] = '\x81';
        buf[1] = (char)m_request.command();
        cybozu::hton(key_len, buf+2);
        buf[4] = (char)extras_len;
        buf[5] = '\x00';
        cybozu::hton((std::uint16_t)status, buf+6);
        std::uint32_t total_len = key_len + extras_len + data_len;
        cybozu::hton(total_len, buf+8);
        std::memcpy(buf+12, m_request.opaque(), 4);
        cybozu::hton(cas, buf+16);
    }
};

}} // namespace yrmcds::memcache

#endif // YRMCDS_MEMCACHE_HPP
