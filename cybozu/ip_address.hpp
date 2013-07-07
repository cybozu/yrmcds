// Abstract IP address.
// (C) 2013 Cybozu.

#ifndef CYBOZU_IP_ADDRESS_HPP
#define CYBOZU_IP_ADDRESS_HPP

#include <arpa/inet.h>
#include <stdexcept>
#include <string>

namespace cybozu {

// Abstract IP address.
//
// Abstract IP address.
// Both IPv4 and IPv6 addresses can be stored transparently.
class ip_address {
    enum class addr_family {none, ipv4, ipv6} af = addr_family::none;
    union {
        struct in_addr  ipv4_addr;
        struct in6_addr ipv6_addr;
    } addr;

public:
    ip_address() noexcept {}
    explicit ip_address(const std::string& s);
    explicit ip_address(const struct sockaddr* ifa_addr);

    struct bad_address: public std::runtime_error {
        bad_address(const std::string& s): std::runtime_error(s) {}
    };

    // This may throw <bad_address> is `s` is not a valid IP address.
    void parse(const std::string& s);

    bool is_v4() const {
        return af == addr_family::ipv4;
    }
    bool is_v6() const {
        return af == addr_family::ipv6;
    }
    const struct in_addr* v4addr() const {
        return &(addr.ipv4_addr);
    }
    const struct in6_addr* v6addr() const {
        return &(addr.ipv6_addr);
    }
    bool operator==(const ip_address& rhs) const;
    std::string str() const;
};


// `true` if this machine has `addr`.  `false` otherwise.
bool has_ip_address(const ip_address& addr);

} // namespace cybozu

#endif // CYBOZU_IP_ADDRESS_HPP
