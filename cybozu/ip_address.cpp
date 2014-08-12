// (C) 2013 Cybozu.

#include "ip_address.hpp"
#include "util.hpp"

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <ifaddrs.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>

namespace {

class ifaddrs_wrapper {
    struct ifaddrs* m_addr = nullptr;
public:
    ifaddrs_wrapper() {
        if( getifaddrs(&m_addr) != 0 )
            cybozu::throw_unix_error(errno, "getifaddrs");
    }
    ~ifaddrs_wrapper() {
        if( m_addr != nullptr )
            freeifaddrs(m_addr);
    }

    bool find(const cybozu::ip_address& addr) const {
        for(auto a = m_addr; a != nullptr; a = a->ifa_next) {
            if( a->ifa_addr == NULL ) continue;
            switch(a->ifa_addr->sa_family) {
            case AF_INET:
            case AF_INET6:
                if( addr == cybozu::ip_address(a->ifa_addr) )
                    return true;
            }
        }
        return false;
    }
};

} // anonymous namespace

namespace cybozu {

void ip_address::parse(const std::string& s) {
    struct addrinfo hints;
    struct addrinfo* res = nullptr;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_flags = AI_NUMERICHOST;
    int e = getaddrinfo(s.c_str(), nullptr, &hints, &res);
    if( e == 0 ) {
        af = addr_family::ipv4;
        std::memcpy(&addr, res->ai_addr, res->ai_addrlen);
        freeaddrinfo(res);
        return;
    }
    if( e != EAI_ADDRFAMILY && e != EAI_NONAME )
        throw std::runtime_error(gai_strerror(e));

    hints.ai_family = AF_INET6;
    e = getaddrinfo(s.c_str(), nullptr, &hints, &res);
    if( e == 0 ) {
        af = addr_family::ipv6;
        std::memcpy(&addr, res->ai_addr, res->ai_addrlen);
        freeaddrinfo(res);
        return;
    }
    if( e != EAI_ADDRFAMILY && e != EAI_NONAME )
        throw std::runtime_error(gai_strerror(e));
    throw bad_address("Invalid address: " + s);
}

ip_address::ip_address(const std::string& s) {
    parse(s);
}

ip_address::ip_address(const struct sockaddr* ifa_addr) {
    if( ifa_addr->sa_family == AF_INET ) {
        af = addr_family::ipv4;
        std::memcpy(&addr, ifa_addr, sizeof(struct sockaddr_in));
        return;
    }
    if( ifa_addr->sa_family == AF_INET6 ) {
        af = addr_family::ipv6;
        std::memcpy(&addr, ifa_addr, sizeof(struct sockaddr_in6));
        return;
    }
    throw bad_address("Invalid address family");
}

bool ip_address::operator==(const ip_address& rhs) const {
    if( af != rhs.af ) return false;
    if( af == addr_family::none ) return true;
    if( is_v4() ) {
        return std::memcmp(v4addr(), rhs.v4addr(), sizeof(struct in_addr)) == 0;
    }
    if( v6scope() != rhs.v6scope() ) return false;
    return std::memcmp(v6addr(), rhs.v6addr(), sizeof(struct in6_addr)) == 0;
}

std::string ip_address::str() const {
    char host[101];
    if( is_v4() || is_v6() ) {
        std::size_t slen = is_v4() ?
            sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);
        int e = getnameinfo((const struct sockaddr*)&addr, (socklen_t)slen,
                            host, sizeof(host), nullptr, 0, NI_NUMERICHOST);
        if( e != 0 ) throw std::runtime_error(gai_strerror(e));
        return std::string(host);
    }
    return "";
}

bool has_ip_address(const ip_address& addr) {
    ifaddrs_wrapper ifw;
    return ifw.find(addr);
}

} // namespace cybozu
