// (C) 2013 Cybozu.

#include "memcache.hpp"
#include "replication.hpp"

#include <cybozu/logger.hpp>
#include <cybozu/util.hpp>

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>

namespace {

using namespace yrmcds::memcache;

const int BINARY_HEADER_SIZE = 24;

inline void
fill_header(char* buf, std::uint16_t key_len, std::uint8_t extras_len,
            std::uint32_t data_len, binary_command cmd) noexcept {
        std::memset(buf, 0, BINARY_HEADER_SIZE);
        buf[0] = '\x80';
        buf[1] = (char)cmd;
        cybozu::hton(key_len, buf+2);
        buf[4] = (char)extras_len;
        std::uint32_t total_len = key_len + extras_len + data_len;
        cybozu::hton(total_len, buf+8);
}

} // anonymous namespace

namespace yrmcds {

void repl_object(const std::vector<cybozu::tcp_socket*>& slaves,
                 const cybozu::hash_key& key, const object& obj,
                 bool flush) {
    char header[BINARY_HEADER_SIZE];
    fill_header(header, key.length(), 8, obj.size(), binary_command::SetQ);
    char extras[8];
    cybozu::hton(obj.flags(), extras);
    cybozu::hton(obj.exptime(), &extras[4]);
    cybozu::dynbuf buf(0);
    const cybozu::dynbuf& data = obj.data(buf);
    cybozu::tcp_socket::iovec iov[4] = {
        {header, sizeof(header)},
        {extras, sizeof(extras)},
        {key.data(), key.length()},
        {data.data(), data.size()}
    };
    for( cybozu::tcp_socket* s: slaves )
        s->sendv(iov, 4, flush);
}

void repl_delete(const std::vector<cybozu::tcp_socket*>& slaves,
                 const cybozu::hash_key& key) {
    char header[BINARY_HEADER_SIZE];
    fill_header(header, key.length(), 0, 0, binary_command::DeleteQ);
    cybozu::tcp_socket::iovec iov[2] = {
        {header, sizeof(header)},
        {key.data(), key.length()}
    };
    for( cybozu::tcp_socket* s: slaves )
        s->sendv(iov, 2, false);
}

std::size_t repl_recv(const char* p, std::size_t len,
                      cybozu::hash_map<object>& hash) {
    namespace mc = yrmcds::memcache;

    std::size_t consumed = 0;
    while( len > 0 ) {
        if( ! mc::is_binary_request(p) )
            throw std::runtime_error("Invalid replication data");

        mc::binary_request parser(p, len);
        std::size_t n = parser.length();
        if( n == 0 ) break;
        p += n;
        len -= n;
        consumed += n;

        const char* key_data;
        std::size_t key_len;
        cybozu::hash_map<object>::handler h = nullptr;
        cybozu::hash_map<object>::creator c = nullptr;

        switch( parser.command() ) {
        case mc::binary_command::SetQ:
            h = [&parser](const cybozu::hash_key&, object& obj) -> bool {
                const char* p2;
                std::size_t len2;
                std::tie(p2, len2) = parser.data();
                obj.set(p2, len2, parser.flags(), parser.exptime());
                return true;
            };
            c = [&parser](const cybozu::hash_key&) -> object {
                const char* p2;
                std::size_t len2;
                std::tie(p2, len2) = parser.data();
                return object(p2, len2, parser.flags(), parser.exptime());
            };
            std::tie(key_data, key_len) = parser.key();
            cybozu::logger::debug() << "repl: set "
                                    << std::string(key_data, key_len);
            hash.apply_nolock(cybozu::hash_key(key_data, key_len), h, c);
            break;
        case mc::binary_command::DeleteQ:
            std::tie(key_data, key_len) = parser.key();
            cybozu::logger::debug() << "repl: remove "
                                    << std::string(key_data, key_len);
            hash.remove_nolock(cybozu::hash_key(key_data, key_len), nullptr);
            break;
        default:
            cybozu::logger::error() << "Unknown replication command"
                                    << std::hex
                                    << (unsigned int)parser.command();
        }
    }
    return consumed;
}

} // namespace yrmcds
