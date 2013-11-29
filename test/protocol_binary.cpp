#include "../src/memcache.hpp"

#include <cybozu/dynbuf.hpp>
#include <cybozu/tcp.hpp>
#define TEST_DISABLE_AUTO_RUN
#include <cybozu/test.hpp>
#include <cybozu/util.hpp>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>
#include <vector>

using yrmcds::memcache::binary_command;
using yrmcds::memcache::binary_status;
using yrmcds::memcache::item;

const char* g_server = nullptr;
std::uint16_t g_port = 11211;

typedef char opaque_t[4];
const std::size_t BINARY_HEADER_SIZE = 24;

int connect_server() {
    int s = cybozu::tcp_connect(g_server, g_port);
    if( s == -1 ) return -1;
    ::fcntl(s, F_SETFL, ::fcntl(s, F_GETFL, 0) & ~O_NONBLOCK);
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 300000;
    ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    int ok = 1;
    ::setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &ok, sizeof(ok));
    return s;
}


// compose binary request
class request {
public:
    request(binary_command cmd,
            std::uint16_t key_len, const char* key,
            char extras_len, const char* extras,
            std::uint32_t data_len, const char* data,
            opaque_t* opaque = nullptr, std::uint64_t cas = 0):
        m_data(BINARY_HEADER_SIZE + key_len + extras_len + data_len),
        m_p(&m_data[0])
    {
        m_p[0] = '\x80';
        m_p[1] = (char)cmd;
        cybozu::hton(key_len, m_p+2);
        m_p[4] = extras_len;
        m_p[5] = 0;
        m_p[6] = 0;
        m_p[7] = 0;
        std::uint32_t total_len = key_len + extras_len + data_len;
        cybozu::hton(total_len, m_p+8);
        if( opaque != nullptr )
            std::memcpy(m_p+12, opaque, sizeof(opaque_t));
        cybozu::hton(cas, m_p+16);

        char* p = m_p + BINARY_HEADER_SIZE;
        std::memcpy(p, extras, extras_len);
        p += extras_len;
        std::memcpy(p, key, key_len);
        p += key_len;
        std::memcpy(p, data, data_len);
    }

    const char* data() {
        return m_p;
    }

    std::size_t length() const noexcept {
        return m_data.size();
    }

private:
    std::vector<char> m_data;
    char* const m_p;
};


// parse binary response
class response {
public:
    response() {}
    response(response&&) noexcept = default;

    // Return length of the response.
    //
    // Return length of the response.
    // If the response is incomplete, zero is returned.
    std::size_t length() const noexcept { return m_response_len; }

    // Response status, if determined by the request.
    binary_status status() const noexcept { return m_status; }

    // Return `true` if the response status was not OK.
    bool error() {
        return m_status != binary_status::OK;
    }

    // Return the command type.
    binary_command command() const noexcept { return m_command; }

    // Return `key`.
    item key() const noexcept { return m_key; }

    // Return `opaque` sent with the request.
    const char* opaque() const noexcept { return m_p + 12; }

    // Return `cas unique` sent with CAS command.
    std::uint64_t cas_unique() const noexcept { return m_cas_unique; }

    // Return `flags` sent with storage commands.
    std::uint32_t flags() const noexcept { return m_flags; }

    // Return data block sent with storage commands.
    item data() const noexcept { return m_data; }

    // Return an unsigned 64bit integer value for increment or decrement.
    std::uint64_t value() const noexcept { return m_value; }

    bool parse(const char* p, std::size_t len);

private:
    const char* m_p = nullptr;
    std::size_t m_len = 0;
    std::size_t m_response_len = 0;
    binary_status m_status;
    binary_command m_command;
    item m_key;
    std::uint64_t m_cas_unique;
    std::uint32_t m_flags = 0;
    item m_data;
    std::uint64_t m_value = 0;
};

bool response::parse(const char* p, std::size_t len) {
    m_p = p;
    m_len = len;
    if( m_len < BINARY_HEADER_SIZE )
        return false;
    cybozu_assert( *m_p == '\x81' );
    std::uint32_t total_len;
    cybozu::ntoh(m_p + 8, total_len);
    if( m_len < (BINARY_HEADER_SIZE + total_len) )
        return false;
    m_response_len = BINARY_HEADER_SIZE + total_len;

    m_command = (binary_command)*(unsigned char*)(m_p + 1);
    std::uint16_t key_len;
    cybozu::ntoh(m_p + 2, key_len);
    std::uint8_t extras_len = *(unsigned char*)(m_p + 4);
    cybozu_assert( total_len >= (key_len + extras_len) );
    if( key_len > 0 ) {
        m_key = item(m_p + (BINARY_HEADER_SIZE + extras_len), key_len);
    } else {
        m_key = item(nullptr, 0);
    }

    std::uint16_t i_status;
    cybozu::ntoh(m_p + 6, i_status);
    m_status = (binary_status)i_status;
    cybozu::ntoh(m_p + 16, m_cas_unique);

    std::size_t data_len = total_len - key_len - extras_len;
    if( data_len > 0 ) {
        m_data = item(m_p + (BINARY_HEADER_SIZE + extras_len + key_len),
                      data_len);
    } else {
        m_data = item(nullptr, 0);
    }

    if( extras_len > 0 ) {
        cybozu_assert( extras_len == 4 );
        cybozu::ntoh(m_p + BINARY_HEADER_SIZE, m_flags);
    }

    if( (m_command == binary_command::Increment ||
         m_command == binary_command::Decrement) &&
        m_status == binary_status::OK ) {
        cybozu_assert( data_len == 8 );
        if( data_len == 8 ) {
            cybozu::ntoh(m_p + BINARY_HEADER_SIZE + key_len + extras_len,
                         m_value);
        }
    }
    return true;
}


class client {
public:
    client(): m_socket(connect_server()), m_buffer(1 << 20) {
        cybozu_assert( m_socket != -1 );
    }
    ~client() { ::close(m_socket); }

    bool get_response(response& resp) {
        m_buffer.erase(m_last_response_size);
        m_last_response_size = 0;
        if( m_buffer.empty() ) {
            if( ! recv() ) {
                cybozu_assert( m_buffer.empty() );
                return false;
            }
        }
        while( true ) {
            if( resp.parse(m_buffer.data(), m_buffer.size()) ) {
                m_last_response_size = resp.length();
                return true;
            }
            cybozu_assert( recv() );
        }
    }

    void noop(opaque_t* opaque) {
        request req(binary_command::Noop,
                    0, nullptr, 0, nullptr, 0, nullptr, opaque);
        send(req.data(), req.length());
    }

    void get(const std::string& key, bool q) {
        request req(q ? binary_command::GetQ : binary_command::Get,
                    (std::uint16_t)key.size(), key.data(),
                    0, nullptr, 0, nullptr);
        send(req.data(), req.length());
    }

    void getk(const std::string& key, bool q) {
        request req(q ? binary_command::GetKQ : binary_command::GetK,
                    (std::uint16_t)key.size(), key.data(),
                    0, nullptr, 0, nullptr);
        send(req.data(), req.length());
    }

    void touch(const std::string& key, std::uint32_t expire) {
        char extra[4];
        cybozu::hton(expire, extra);
        request req(binary_command::Touch,
                    (std::uint16_t)key.size(), key.data(),
                    sizeof(extra), extra, 0, nullptr);
        send(req.data(), req.length());
    }

    void get_and_touch(const std::string& key, std::uint32_t expire, bool q) {
        char extra[4];
        cybozu::hton(expire, extra);
        request req(q ? binary_command::GaTQ : binary_command::GaT,
                    (std::uint16_t)key.size(), key.data(),
                    sizeof(extra), extra, 0, nullptr);
        send(req.data(), req.length());
    }

    void getk_and_touch(const std::string& key, std::uint32_t expire, bool q) {
        char extra[4];
        cybozu::hton(expire, extra);
        request req(q ? binary_command::GaTKQ : binary_command::GaTK,
                    (std::uint16_t)key.size(), key.data(),
                    sizeof(extra), extra, 0, nullptr);
        send(req.data(), req.length());
    }

    void lock_and_get(const std::string& key, bool q) {
        request req(q ? binary_command::LaGQ : binary_command::LaG,
                    (std::uint16_t)key.size(), key.data(),
                    0, nullptr, 0, nullptr);
        send(req.data(), req.length());
    }

    void lock_and_getk(const std::string& key, bool q) {
        request req(q ? binary_command::LaGKQ : binary_command::LaGK,
                    (std::uint16_t)key.size(), key.data(),
                    0, nullptr, 0, nullptr);
        send(req.data(), req.length());
    }

    void set(const std::string& key, const std::string& data, bool q,
             std::uint32_t flags, std::uint32_t expire, std::uint64_t cas = 0) {
        char extra[8];
        cybozu::hton(flags, extra);
        cybozu::hton(expire, &extra[4]);
        request req(q ? binary_command::SetQ : binary_command::Set,
                    (std::uint16_t)key.size(), key.data(),
                    sizeof(extra), extra,
                    (std::uint32_t)data.length(), data.data(),
                    nullptr, cas);
        send(req.data(), req.length());
    }

    void replace(const std::string& key, const std::string& data, bool q,
                 std::uint32_t flags, std::uint32_t expire,
                 std::uint64_t cas = 0) {
        char extra[8];
        cybozu::hton(flags, extra);
        cybozu::hton(expire, &extra[4]);
        request req(q ? binary_command::ReplaceQ : binary_command::Replace,
                    (std::uint16_t)key.size(), key.data(),
                    sizeof(extra), extra,
                    (std::uint32_t)data.length(), data.data(),
                    nullptr, cas);
        send(req.data(), req.length());
    }

    void add(const std::string& key, const std::string& data, bool q,
             std::uint32_t flags, std::uint32_t expire, std::uint64_t cas = 0) {
        char extra[8];
        cybozu::hton(flags, extra);
        cybozu::hton(expire, &extra[4]);
        request req(q ? binary_command::AddQ : binary_command::Add,
                    (std::uint16_t)key.size(), key.data(),
                    sizeof(extra), extra,
                    (std::uint32_t)data.length(), data.data(),
                    nullptr, cas);
        send(req.data(), req.length());
    }

    void replace_and_unlock(const std::string& key, const std::string& data,
                            bool q, std::uint32_t flags, std::uint32_t expire,
                            std::uint64_t cas = 0) {
        char extra[8];
        cybozu::hton(flags, extra);
        cybozu::hton(expire, &extra[4]);
        request req(q ? binary_command::RaUQ : binary_command::RaU,
                    (std::uint16_t)key.size(), key.data(),
                    sizeof(extra), extra,
                    (std::uint32_t)data.length(), data.data(),
                    nullptr, cas);
        send(req.data(), req.length());
    }

    void incr(const std::string& key, std::uint64_t value, bool q,
              std::uint32_t expire = ~(std::uint32_t)0,
              std::uint64_t initial = 0) {
        char extra[20];
        cybozu::hton(value, extra);
        cybozu::hton(initial, &extra[8]);
        cybozu::hton(expire, &extra[16]);
        request req(q ? binary_command::IncrementQ : binary_command::Increment,
                    (std::uint16_t)key.size(), key.data(),
                    sizeof(extra), extra, 0, nullptr);
        send(req.data(), req.length());
    }

    void decr(const std::string& key, std::uint64_t value, bool q,
              std::uint32_t expire = ~(std::uint32_t)0,
              std::uint64_t initial = 0) {
        char extra[20];
        cybozu::hton(value, extra);
        cybozu::hton(initial, &extra[8]);
        cybozu::hton(expire, &extra[16]);
        request req(q ? binary_command::DecrementQ : binary_command::Decrement,
                    (std::uint16_t)key.size(), key.data(),
                    sizeof(extra), extra, 0, nullptr);
        send(req.data(), req.length());
    }

    void append(const std::string& key, const std::string& value, bool q) {
        request req(q ? binary_command::AppendQ : binary_command::Append,
                    (std::uint16_t)key.size(), key.data(), 0, nullptr,
                    (std::uint32_t)value.size(), value.data());
        send(req.data(), req.length());
    }

    void prepend(const std::string& key, const std::string& value, bool q) {
        request req(q ? binary_command::PrependQ : binary_command::Prepend,
                    (std::uint16_t)key.size(), key.data(), 0, nullptr,
                    (std::uint32_t)value.size(), value.data());
        send(req.data(), req.length());
    }

    void remove(const std::string& key, bool q) {
        request req(q ? binary_command::DeleteQ : binary_command::Delete,
                    (std::uint16_t)key.size(), key.data(),
                    0, nullptr, 0, nullptr);
        send(req.data(), req.length());
    }

    void lock(const std::string& key, bool q) {
        request req(q ? binary_command::LockQ : binary_command::Lock,
                    (std::uint16_t)key.size(), key.data(),
                    0, nullptr, 0, nullptr);
        send(req.data(), req.length());
    }

    void unlock(const std::string& key, bool q) {
        request req(q ? binary_command::UnlockQ : binary_command::Unlock,
                    (std::uint16_t)key.size(), key.data(),
                    0, nullptr, 0, nullptr);
        send(req.data(), req.length());
    }

    void unlock_all(bool q) {
        request req(q ? binary_command::UnlockAllQ : binary_command::UnlockAll,
                    0, nullptr, 0, nullptr, 0, nullptr);
        send(req.data(), req.length());
    }

    void flush_all(bool q, std::uint32_t expire = 0) {
        if( expire == 0 ) {
            request req(q ? binary_command::FlushQ : binary_command::Flush,
                        0, nullptr, 0, nullptr, 0, nullptr);
            send(req.data(), req.length());
            return;
        }
        char extra[4];
        cybozu::hton(expire, extra);
        request req(q ? binary_command::FlushQ : binary_command::Flush,
                    0, nullptr, sizeof(extra), extra, 0, nullptr);
        send(req.data(), req.length());
    }

    void version() {
        request req(binary_command::Version,
                    0, nullptr, 0, nullptr, 0, nullptr);
        send(req.data(), req.length());
    }

    void stat() {
        request req(binary_command::Stat,
                    0, nullptr, 0, nullptr, 0, nullptr);
        send(req.data(), req.length());
    }

    void stat(const std::string& key) {
        request req(binary_command::Stat,
                    (std::uint16_t)key.size(), key.data(),
                    0, nullptr, 0, nullptr);
        send(req.data(), req.length());
    }

    void quit(bool q) {
        request req(q ? binary_command::QuitQ : binary_command::Quit,
                    0, nullptr, 0, nullptr, 0, nullptr);
        send(req.data(), req.length());
    }

private:
    const int m_socket;
    cybozu::dynbuf m_buffer;
    std::size_t m_last_response_size = 0;

    void send(const char* p, std::size_t len) {
        while( len > 0 ) {
            ssize_t n = ::send(m_socket, p, len, 0);
            cybozu_assert( n != -1 );
            p += n;
            len -= n;
        }
    }

    bool recv() {
        char* p = m_buffer.prepare(256 << 10);
        ssize_t n = ::recv(m_socket, p, 256<<10, 0);
        cybozu_assert( n != -1 );
        if( n == -1 || n == 0 ) return false;
        m_buffer.consume(n);
        return true;
    }
};


void print_item(const item& i) {
    std::cout << std::string(std::get<0>(i), std::get<1>(i)) << std::endl;
}

bool itemcmp(const item& i, const std::string& s) {
    if( s.size() != std::get<1>(i) ) return false;
    return std::memcmp(s.data(), std::get<0>(i), s.size()) == 0;
}

// tests
#define ASSERT_COMMAND(r,cmd) cybozu_assert( r.command() == binary_command::cmd )
#define ASSERT_OK(r) cybozu_assert( r.status() == binary_status::OK )

AUTOTEST(opaque) {
    client c;
    response r;

    c.noop(nullptr);
    cybozu_assert( c.get_response(r) );
    ASSERT_COMMAND(r, Noop);
    ASSERT_OK(r);
    opaque_t zero = {'\0', '\0', '\0', '\0'};
    cybozu_assert( std::memcmp(r.opaque(), &zero, sizeof(opaque_t)) == 0 );

    opaque_t op1 = {'\x12', '\x23', '\x45', '\x67'};
    c.noop(&op1);
    cybozu_assert( c.get_response(r) );
    ASSERT_COMMAND(r, Noop);
    ASSERT_OK(r);
    cybozu_assert( std::memcmp(r.opaque(), &op1, sizeof(opaque_t)) == 0 );
}

AUTOTEST(quit) {
    client c;
    response r;
    c.quit(false);
    cybozu_assert( c.get_response(r) );
    ASSERT_COMMAND(r, Quit);
    ASSERT_OK( r );
}

AUTOTEST(quitq) {
    client c;
    response r;
    c.quit(true);
    cybozu_assert( ! c.get_response(r) );
}

AUTOTEST(version) {
    client c;
    response r;
    c.version();
    cybozu_assert( c.get_response(r) );
    print_item( r.data() );
}

AUTOTEST(set) {
    client c;
    response r;

    c.set("hello", "world", false, 123, 0);
    cybozu_assert( c.get_response(r) );
    ASSERT_COMMAND(r, Set);
    ASSERT_OK(r);

    c.set("hello", "world!", false, 111, 0, r.cas_unique());
    cybozu_assert( c.get_response(r) );
    ASSERT_COMMAND(r, Set);
    ASSERT_OK(r);

    c.set("hello", "world!!", true, 111, 0, r.cas_unique() + 1);
    cybozu_assert( c.get_response(r) );
    ASSERT_COMMAND(r, SetQ);
    cybozu_assert( r.status() == binary_status::Exists );

    c.set("not exist", "hoge", true, 111, 0, 100);
    cybozu_assert( c.get_response(r) );
    ASSERT_COMMAND(r, SetQ);
    cybozu_assert( r.status() == binary_status::NotFound );

    c.set("abc", "def", true, 123, 0);
    c.noop(nullptr);
    cybozu_assert( c.get_response(r) );
    ASSERT_COMMAND(r, Noop);

    c.get("abc", false);
    cybozu_assert( c.get_response(r) );
    ASSERT_COMMAND(r, Get);
    ASSERT_OK(r);
    cybozu_assert( itemcmp(r.data(), "def") );
    cybozu_assert( r.flags() == 123 );

    c.get("not exist", false);
    cybozu_assert( c.get_response(r) );
    ASSERT_COMMAND(r, Get);
    cybozu_assert( r.status() == binary_status::NotFound );

    c.get("not exist", true);
    c.noop(nullptr);
    cybozu_assert( c.get_response(r) );
    ASSERT_COMMAND(r, Noop);
    ASSERT_OK(r);
}

AUTOTEST(expire) {
    client c;
    response r;

    c.set("abc", "def1", true, 123, 1);
    c.get("abc", false);
    cybozu_assert( c.get_response(r) );
    ASSERT_COMMAND(r, Get);
    ASSERT_OK(r);

    std::this_thread::sleep_for(std::chrono::seconds(3));
    c.get("abc", false);
    cybozu_assert( c.get_response(r) );
    cybozu_assert( r.status() == binary_status::NotFound );
}

AUTOTEST(touch) {
    client c;
    response r;

    c.remove("abc", true);
    c.touch("abc", 0);
    cybozu_assert( c.get_response(r) );
    ASSERT_COMMAND(r, Touch);
    cybozu_assert( r.status() == binary_status::NotFound );

    c.set("abc", "def", true, 10, 2);
    c.touch("abc", 0);
    cybozu_assert( c.get_response(r) );
    ASSERT_COMMAND(r, Touch);
    ASSERT_OK(r);

    std::this_thread::sleep_for(std::chrono::seconds(4));
    c.get("abc", false);
    cybozu_assert( c.get_response(r) );
    ASSERT_OK(r);

    c.touch("abc", 1);
    cybozu_assert( c.get_response(r) );
    ASSERT_OK(r);

    std::this_thread::sleep_for(std::chrono::seconds(3));
    c.get("abc", false);
    cybozu_assert( c.get_response(r) );
    cybozu_assert( r.status() == binary_status::NotFound );
}

AUTOTEST(add) {
    client c;
    response r;

    c.set("abc", "def", true, 0, 0);
    c.add("abc", "123", true, 0, 0);
    cybozu_assert( c.get_response(r) );
    ASSERT_COMMAND(r, AddQ);
    cybozu_assert( r.status() == binary_status::NotStored );

    c.remove("abc", true);
    c.add("abc", "123", false, 0, 0);
    cybozu_assert( c.get_response(r) );
    ASSERT_COMMAND(r, Add);
    ASSERT_OK(r);
}

AUTOTEST(replace) {
    client c;
    response r;

    c.replace("not exist", "hoge", true, 100, 0);
    cybozu_assert( c.get_response(r) );
    ASSERT_COMMAND(r, ReplaceQ);
    cybozu_assert( r.status() == binary_status::NotStored );

    c.set("abc", "def", true, 10, 0);
    c.replace("abc", "123", false, 100, 0);
    cybozu_assert( c.get_response(r) );
    ASSERT_COMMAND(r, Replace);
    ASSERT_OK(r);

    c.get("abc", false);
    cybozu_assert( c.get_response(r) );
    ASSERT_COMMAND(r, Get);
    ASSERT_OK(r);
    cybozu_assert( itemcmp(r.data(), "123") );
    cybozu_assert( r.flags() == 100 );
}

AUTOTEST(get) {
    client c;
    response r;

    c.remove("unique key", true);

    c.add("unique key", "value", false, 10, 0);
    cybozu_assert( c.get_response(r) );
    ASSERT_OK(r);
    c.replace("unique key", "value2", false, 11, 0);
    cybozu_assert( c.get_response(r) );
    ASSERT_OK(r);

    std::uint64_t cas = r.cas_unique();

    c.get("unique key", false);
    cybozu_assert( c.get_response(r) );
    ASSERT_COMMAND(r, Get);
    ASSERT_OK(r);
    cybozu_assert( r.key() == item(nullptr, 0) );
    cybozu_assert( itemcmp(r.data(), "value2") );
    cybozu_assert( r.flags() == 11 );
    cybozu_assert( r.cas_unique() == cas );

    c.getk("unique key", false);
    cybozu_assert( c.get_response(r) );
    ASSERT_COMMAND(r, GetK);
    ASSERT_OK(r);
    cybozu_assert( itemcmp(r.key(), "unique key") );
    cybozu_assert( itemcmp(r.data(), "value2") );
    cybozu_assert( r.flags() == 11 );
    cybozu_assert( r.cas_unique() == cas );
}

AUTOTEST(get_and_touch) {
    client c;
    response r;

    c.set("abc", "def", true, 10, 2);
    c.get_and_touch("abc", 0, false);
    cybozu_assert( c.get_response(r) );
    ASSERT_COMMAND(r, GaT);
    ASSERT_OK(r);
    cybozu_assert( std::get<1>(r.key()) == 0 );
    cybozu_assert( itemcmp(r.data(), "def") );

    std::this_thread::sleep_for(std::chrono::seconds(4));
    c.getk_and_touch("abc", 1, false);
    cybozu_assert( c.get_response(r) );
    ASSERT_COMMAND(r, GaTK);
    ASSERT_OK(r);
    cybozu_assert( itemcmp(r.key(), "abc") );
    cybozu_assert( itemcmp(r.data(), "def") );

    std::this_thread::sleep_for(std::chrono::seconds(3));
    c.touch("abc", 0);
    cybozu_assert( c.get_response(r) );
    cybozu_assert( r.status() == binary_status::NotFound );
}

AUTOTEST(incr_decr) {
    client c;
    response r;

    c.set("abc", "def", true, 10, 0);
    c.incr("abc", 10, true);
    cybozu_assert( c.get_response(r) );
    ASSERT_COMMAND(r, IncrementQ);
    cybozu_assert( r.status() == binary_status::NonNumeric );

    c.remove("abc", true);
    c.incr("abc", 10, true);
    cybozu_assert( c.get_response(r) );
    ASSERT_COMMAND(r, IncrementQ);
    cybozu_assert( r.status() == binary_status::NotFound );

    c.incr("abc", 10, false, 0, 12);
    cybozu_assert( c.get_response(r) );
    ASSERT_COMMAND(r, Increment);
    ASSERT_OK(r);
    cybozu_assert( r.value() == 12 );
    cybozu_assert( r.flags() == 0 );

    c.incr("abc", 10, false);
    cybozu_assert( c.get_response(r) );
    ASSERT_COMMAND(r, Increment);
    ASSERT_OK(r);
    cybozu_assert( r.value() == 22 );

    c.decr("abc", 1, false);
    cybozu_assert( c.get_response(r) );
    ASSERT_COMMAND(r, Decrement);
    ASSERT_OK(r);
    cybozu_assert( r.value() == 21 );

    c.decr("abc", 100, false);
    cybozu_assert( c.get_response(r) );
    ASSERT_COMMAND(r, Decrement);
    ASSERT_OK(r);
    cybozu_assert( r.value() == 0 );

    c.remove("abc", true);
    c.decr("abc", 1, true);
    cybozu_assert( c.get_response(r) );
    ASSERT_COMMAND(r, DecrementQ);
    cybozu_assert( r.status() == binary_status::NotFound );

    c.set("abc", "def", true, 10, 0);
    c.decr("abc", 1, true);
    cybozu_assert( c.get_response(r) );
    ASSERT_COMMAND(r, DecrementQ);
    cybozu_assert( r.status() == binary_status::NonNumeric );

    c.remove("abc", true);
    c.decr("abc", 10, false, 0, 22);
    cybozu_assert( c.get_response(r) );
    ASSERT_COMMAND(r, Decrement);
    ASSERT_OK(r);
    cybozu_assert( r.value() == 22 );
}

AUTOTEST(append_prepend) {
    client c;
    response r;

    c.remove("ttt", true);
    c.append("ttt", "111", true);
    cybozu_assert( c.get_response(r) );
    ASSERT_COMMAND(r, AppendQ);
    cybozu_assert( r.status() == binary_status::NotFound );

    c.set("ttt", "aaa", true, 0, 0);
    c.append("ttt", "111", false);
    cybozu_assert( c.get_response(r) );
    ASSERT_COMMAND(r, Append);
    ASSERT_OK(r);

    c.get("ttt", false);
    cybozu_assert( c.get_response(r) );
    ASSERT_OK(r);
    cybozu_assert( itemcmp(r.data(), "aaa111") );

    c.prepend("ttt", "222", false);
    cybozu_assert( c.get_response(r) );
    ASSERT_COMMAND(r, Prepend);
    ASSERT_OK(r);

    c.get("ttt", false);
    cybozu_assert( c.get_response(r) );
    ASSERT_OK(r);
    cybozu_assert( itemcmp(r.data(), "222aaa111") );

    c.remove("ttt", true);
    c.prepend("ttt", "111", true);
    cybozu_assert( c.get_response(r) );
    ASSERT_COMMAND(r, PrependQ);
    cybozu_assert( r.status() == binary_status::NotFound );
}

AUTOTEST(delete) {
    client c;
    response r;

    c.set("abc", "def", true, 10, 0);
    c.remove("abc", false);
    cybozu_assert( c.get_response(r) );
    ASSERT_COMMAND(r, Delete);
    ASSERT_OK(r);

    c.remove("abc", false);
    cybozu_assert( c.get_response(r) );
    ASSERT_COMMAND(r, Delete);
    cybozu_assert( r.status() == binary_status::NotFound );

    c.remove("abc", true);
    c.noop(nullptr);
    cybozu_assert( c.get_response(r) );
    ASSERT_COMMAND(r, Noop);
}

AUTOTEST(lock) {
    client c;
    response r;

    c.set("abc", "10", false, 0, 0);
    cybozu_assert( c.get_response(r) );

    {
        client c2;
        c2.lock("abc", false);
        cybozu_assert( c2.get_response(r) );
        ASSERT_COMMAND(r, Lock);
        ASSERT_OK(r);

        c.lock("abc", true);
        cybozu_assert( c.get_response(r) );
        ASSERT_COMMAND(r, LockQ);
        cybozu_assert( r.status() == binary_status::Locked );

        c2.quit(false);
        cybozu_assert( c2.get_response(r) );
    }

    c.lock("abc", false);
    cybozu_assert( c.get_response(r) );
    ASSERT_COMMAND(r, Lock);
    ASSERT_OK(r);

    c.quit(false);
    c.get_response(r);
}

AUTOTEST(unlock) {
    client c;
    response r;

    c.remove("abc", false);
    cybozu_assert( c.get_response(r) );
    ASSERT_OK(r);

    c.lock("abc", true);
    cybozu_assert( c.get_response(r) );
    ASSERT_COMMAND(r, LockQ);
    cybozu_assert( r.status() == binary_status::NotFound );

    c.unlock("abc", true);
    cybozu_assert( c.get_response(r) );
    ASSERT_COMMAND(r, UnlockQ);
    cybozu_assert( r.status() == binary_status::NotFound );

    c.set("abc", "def", true, 10, 0);
    c.unlock("abc", true);
    cybozu_assert( c.get_response(r) );
    ASSERT_COMMAND(r, UnlockQ);
    cybozu_assert( r.status() == binary_status::NotLocked );

    c.lock("abc", false);
    cybozu_assert( c.get_response(r) );
    ASSERT_COMMAND(r, Lock);
    ASSERT_OK(r);

    client c2;
    c2.unlock("abc", true);
    cybozu_assert( c2.get_response(r) );
    ASSERT_COMMAND(r, UnlockQ);
    cybozu_assert( r.status() == binary_status::NotLocked );

    c.unlock("abc", false);
    cybozu_assert( c.get_response(r) );
    ASSERT_COMMAND(r, Unlock);
    ASSERT_OK(r);

    c2.lock("abc", false);
    cybozu_assert( c2.get_response(r) );
    ASSERT_COMMAND(r, Lock);
    ASSERT_OK(r);

    c2.quit(false);
    c2.get_response(r);
}

AUTOTEST(unlock_all) {
    client c;
    response r;

    c.set("abc", "1", true, 10, 0);
    c.set("def", "2", true, 10, 0);
    c.lock("abc", false);
    cybozu_assert( c.get_response(r) );
    ASSERT_OK(r);
    c.lock("def", false);
    cybozu_assert( c.get_response(r) );
    ASSERT_OK(r);

    client c2;
    c2.lock("abc", true);
    cybozu_assert( c2.get_response(r) );
    cybozu_assert( r.status() == binary_status::Locked );
    c2.lock("def", true);
    cybozu_assert( c2.get_response(r) );
    cybozu_assert( r.status() == binary_status::Locked );

    c.unlock_all(false);
    cybozu_assert( c.get_response(r) );
    ASSERT_COMMAND(r, UnlockAll);
    ASSERT_OK(r);

    c2.lock("abc", false);
    cybozu_assert( c2.get_response(r) );
    ASSERT_OK(r);
    c2.lock("def", false);
    cybozu_assert( c2.get_response(r) );
    ASSERT_OK(r);

    c2.quit(false);
    c2.get_response(r);
}

AUTOTEST(lock_and_get) {
    client c;
    response r;

    c.remove("abc", true);
    c.lock_and_get("abc", true);
    cybozu_assert( c.get_response(r) );
    ASSERT_COMMAND(r, LaGQ);
    cybozu_assert( r.status() == binary_status::NotFound );

    c.set("abc", "def", true, 10, 0);
    c.lock_and_get("abc", false);
    cybozu_assert( c.get_response(r) );
    ASSERT_COMMAND(r, LaG);
    ASSERT_OK(r);
    cybozu_assert( std::get<1>(r.key()) == 0 );
    cybozu_assert( itemcmp(r.data(), "def") );
    std::uint64_t cas = r.cas_unique();

    c.lock_and_get("abc", true);
    cybozu_assert( c.get_response(r) );
    cybozu_assert( r.status() == binary_status::Locked );

    client c2;
    c2.lock_and_get("abc", true);
    cybozu_assert( c2.get_response(r) );
    cybozu_assert( r.status() == binary_status::Locked );

    c2.replace_and_unlock("abc", "ghi", true, 20, 0);
    cybozu_assert( c2.get_response(r) );
    ASSERT_COMMAND(r, RaUQ);
    cybozu_assert( r.status() == binary_status::NotLocked );

    c.replace_and_unlock("abc", "ghi", false, 20, 0);
    cybozu_assert( c.get_response(r) );
    ASSERT_COMMAND(r, RaU);
    ASSERT_OK(r);
    cybozu_assert( r.cas_unique() != cas );
    cybozu_assert( r.cas_unique() != 0 );

    c2.lock_and_getk("abc", false);
    cybozu_assert( c2.get_response(r) );
    ASSERT_OK(r);
    cybozu_assert( itemcmp(r.key(), "abc") );
    cybozu_assert( itemcmp(r.data(), "ghi") );

    c2.quit(false);
    c2.get_response(r);
}

AUTOTEST(flush) {
    client c;
    response r;

    c.set("abc", "def", true, 10, 0);
    c.flush_all(true, 2);
    c.get("abc", false);
    cybozu_assert( c.get_response(r) );
    ASSERT_COMMAND(r, Get);
    ASSERT_OK(r);

    std::this_thread::sleep_for(std::chrono::seconds(4));
    c.get("abc", false);
    cybozu_assert( c.get_response(r) );
    ASSERT_COMMAND(r, Get);
    cybozu_assert( r.status() == binary_status::NotFound );

    c.set("abc", "234", true, 10, 0);
    c.flush_all(false);
    cybozu_assert( c.get_response(r) );
    ASSERT_COMMAND(r, Flush);
    ASSERT_OK(r);

    c.get("abc", false);
    cybozu_assert( c.get_response(r) );
    ASSERT_COMMAND(r, Get);
    cybozu_assert( r.status() == binary_status::NotFound );
}

AUTOTEST(stat_general) {
    client c;
    response r;

    c.stat();
    while( true ) {
        cybozu_assert( c.get_response(r) );
        ASSERT_COMMAND(r, Stat);
        ASSERT_OK(r);
        if( std::get<1>(r.key()) == 0 )
            break;
        continue;
        std::cout << std::string(std::get<0>(r.key()), std::get<1>(r.key()))
                  << ": "
                  << std::string(std::get<0>(r.data()), std::get<1>(r.data()))
                  << std::endl;
    }
}

AUTOTEST(stat_settings) {
    client c;
    response r;

    c.stat("settings");
    while( true ) {
        cybozu_assert( c.get_response(r) );
        ASSERT_COMMAND(r, Stat);
        ASSERT_OK(r);
        if( std::get<1>(r.key()) == 0 )
            break;
        continue;
        std::cout << std::string(std::get<0>(r.key()), std::get<1>(r.key()))
                  << ": "
                  << std::string(std::get<0>(r.data()), std::get<1>(r.data()))
                  << std::endl;
    }
}

AUTOTEST(stat_sizes) {
    client c;
    response r;

    c.stat("sizes");
    while( true ) {
        cybozu_assert( c.get_response(r) );
        ASSERT_COMMAND(r, Stat);
        ASSERT_OK(r);
        if( std::get<1>(r.key()) == 0 )
            break;
        continue;
        std::cout << std::string(std::get<0>(r.key()), std::get<1>(r.key()))
                  << ": "
                  << std::string(std::get<0>(r.data()), std::get<1>(r.data()))
                  << std::endl;
    }
}

AUTOTEST(stat_items) {
    client c;
    response r;

    c.stat("items");
    while( true ) {
        cybozu_assert( c.get_response(r) );
        ASSERT_COMMAND(r, Stat);
        ASSERT_OK(r);
        if( std::get<1>(r.key()) == 0 )
            break;
        continue;
        std::cout << std::string(std::get<0>(r.key()), std::get<1>(r.key()))
                  << ": "
                  << std::string(std::get<0>(r.data()), std::get<1>(r.data()))
                  << std::endl;
    }
}

AUTOTEST(stat_ops) {
    client c;
    response r;

    c.stat("ops");
    while( true ) {
        cybozu_assert( c.get_response(r) );
        ASSERT_COMMAND(r, Stat);
        ASSERT_OK(r);
        if( std::get<1>(r.key()) == 0 )
            break;
        continue;
        std::cout << std::string(std::get<0>(r.key()), std::get<1>(r.key()))
                  << ": "
                  << std::string(std::get<0>(r.data()), std::get<1>(r.data()))
                  << std::endl;
    }
}


// main
bool optparse(int argc, char** argv) {
    if( argc != 2 && argc != 3 ) {
        std::cout << "Usage: protocol_binary SERVER [PORT]" << std::endl;
        return false;
    }
    g_server = argv[1];
    if( argc == 3 ) {
        int n = std::stoi(argv[2]);
        if( n <= 0 || n > 65535 ) {
            std::cout << "Invalid port number: " << argv[2] << std::endl;
            return false;
        }
        g_port = n;
    }

    int s = connect_server();
    if( s == -1 ) {
        std::cout << "Failed to connect to " << g_server << std::endl;
        return false;
    }
    ::close(s);
    return true;
}

TEST_MAIN(optparse);
