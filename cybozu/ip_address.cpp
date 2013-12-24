// (C) 2013 Cybozu.

#include "ip_address.hpp"
#include "util.hpp"

#include <ifaddrs.h>
#include <cstring>
#include <netinet/in.h>
#include <sys/types.h>
#include <cerrno>

namespace {

class ifaddrs_wrapper {
    struct ifaddrs* m_addr;
public:
    ifaddrs_wrapper() {
        if( getifaddrs(&m_addr) != 0 )
            cybozu::throw_unix_error(errno, "getifaddrs");
    }
    ~ifaddrs_wrapper() {
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
    if( inet_pton(AF_INET, s.c_str(), &addr) == 1 ) {
        af = addr_family::ipv4;
        return;
    }
    if( inet_pton(AF_INET6, s.c_str(), &addr) == 1 ) {
        af = addr_family::ipv6;
        return;
    }
    throw bad_address("Invalid address: " + s);
}

ip_address::ip_address(const std::string& s) {
    parse(s);
}

ip_address::ip_address(const struct sockaddr* ifa_addr) {
    if( ifa_addr->sa_family == AF_INET ) {
        af = addr_family::ipv4;
        const struct sockaddr_in* p = (const struct sockaddr_in*)ifa_addr;
        std::memcpy(&addr, &(p->sin_addr), sizeof(struct in_addr));
        return;
    }
    if( ifa_addr->sa_family == AF_INET6 ) {
        af = addr_family::ipv6;
        const struct sockaddr_in6* p = (const struct sockaddr_in6*)ifa_addr;
        std::memcpy(&addr, &(p->sin6_addr), sizeof(struct in6_addr));
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
    return std::memcmp(v6addr(), rhs.v6addr(), sizeof(struct in6_addr)) == 0;
}

std::string ip_address::str() const {
    if( is_v4() ) {
        char dst[INET_ADDRSTRLEN];
        return std::string(inet_ntop(AF_INET, v4addr(), dst, INET_ADDRSTRLEN));
    }
    char dst[INET6_ADDRSTRLEN];
    return std::string(inet_ntop(AF_INET6, v6addr(), dst, INET6_ADDRSTRLEN));
}

bool has_ip_address(const ip_address& addr) {
    ifaddrs_wrapper ifw;
    return ifw.find(addr);
}

} // namespace cybozu
