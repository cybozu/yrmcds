// The server configurations.
// (C) 2013 Cybozu.

#ifndef YRMCDS_CONFIG_HPP
#define YRMCDS_CONFIG_HPP

#include "constants.hpp"

#include <cybozu/config_parser.hpp>
#include <cybozu/ip_address.hpp>
#include <cybozu/logger.hpp>

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>

namespace yrmcds {

enum class leader_election_method {
    virtual_ip, file,
};

// Configurations for counter extension.
class counter_config {
public:
    void load(const cybozu::config_parser&);

    bool enable() const noexcept {
        return m_enable;
    }
    std::uint16_t port() const noexcept {
        return m_port;
    }
    unsigned int max_connections() const noexcept {
        return m_max_connections;
    }
    unsigned int buckets() const noexcept {
        return m_buckets;
    }
    unsigned int stat_interval() const noexcept {
        return m_stat_interval;
    }

private:
    bool m_enable = false;
    std::uint16_t m_port = DEFAULT_COUNTER_PORT;
    unsigned int m_max_connections = 0;
    unsigned int m_buckets = DEFAULT_BUCKETS;
    unsigned int m_stat_interval = DEFAULT_STAT_INTERVAL;
};

// Configurations for yrmcds.
//
// Configurations for yrmcds.
// Defaults are configured properly without calling <config::load>.
class config {
public:
    // Setup default configurations.
    config() {
        static_assert( sizeof(std::size_t) >= 4, "std::size_t is too small" );
    }

    struct bad_config: public std::runtime_error {
        bad_config(const std::string& s): std::runtime_error(s) {}
    };

    // Load configurations from `path`.
    //
    // This loads configurations from a file at `path`.
    // This may throw miscellaneous <std::runtime_error> exceptions.
    void load(const std::string& path);

    yrmcds::leader_election_method leader_election_method() const noexcept {
        return m_leader_election_method;
    }
    const std::optional<cybozu::ip_address>& vip() const noexcept {
        return m_vip;
    }
    // Returns the address of the master server.
    // If the leader election method is virtual_ip, this returns "virtual_ip".
    // Otherwise, this returns "master_host".
    std::string master_host() const {
        if( m_master_host ) {
            return *m_master_host;
        } else if( m_vip ) {
            return m_vip->str();
        } else {
            throw bad_config("[bug] both of vip and master_host are not set");
        }
    }
    std::uint16_t port() const noexcept {
        return m_port;
    }
    std::uint16_t repl_port() const noexcept {
        return m_repl_port;
    }
    const std::optional<std::string>& master_file_path() {
        return m_master_file_path;
    }
    const std::vector<cybozu::ip_address>& bind_ip() const noexcept {
        return m_bind_ip;
    }
    unsigned int max_connections() const noexcept {
        return m_max_connections;
    }
    const std::string& tempdir() const noexcept {
        return m_tempdir;
    }
    const std::string& user() const noexcept {
        return m_user;
    }
    const std::string& group() const noexcept {
        return m_group;
    }
    cybozu::severity threshold() const noexcept {
        return m_threshold;
    }
    const std::string& logfile() const noexcept {
        return m_logfile;
    }
    unsigned int buckets() const noexcept {
        return m_buckets;
    }
    std::size_t max_data_size() const noexcept {
        return m_max_data_size;
    }
    std::size_t heap_data_limit() const noexcept {
        return m_heap_data_limit;
    }
    std::size_t memory_limit() const noexcept {
        return m_memory_limit;
    }
    unsigned int repl_bufsize() const noexcept {
        return m_repl_bufsize;
    }
    std::uint64_t initial_repl_sleep_delay_usec() const noexcept {
        return m_initial_repl_sleep_delay_usec;
    }
    bool secure_erase() const noexcept {
        return m_secure_erase;
    }
    bool lock_memory() const noexcept {
        return m_lock_memory;
    }
    unsigned int workers() const noexcept {
        return m_workers;
    }
    unsigned int gc_interval() const noexcept {
        return m_gc_interval;
    }
    unsigned int slave_timeout() const noexcept {
        return m_slave_timeout;
    }

    const counter_config& counter() const noexcept {
        return m_counter_config;
    }

    void set_heap_data_limit(std::size_t new_limit) noexcept {
        m_heap_data_limit = new_limit;
    }

private:
    void sanity_check();

    alignas(CACHELINE_SIZE)
    yrmcds::leader_election_method m_leader_election_method = yrmcds::leader_election_method::virtual_ip;
    std::optional<cybozu::ip_address> m_vip = std::optional(cybozu::ip_address("127.0.0.1"));
    std::optional<std::string> m_master_host;
    std::optional<std::string> m_master_file_path;
    std::uint16_t m_port = DEFAULT_MEMCACHE_PORT;
    std::uint16_t m_repl_port = DEFAULT_REPL_PORT;
    std::vector<cybozu::ip_address> m_bind_ip;
    unsigned int m_max_connections = 0;
    std::string m_tempdir = DEFAULT_TMPDIR;
    std::string m_user;
    std::string m_group;
    cybozu::severity m_threshold = cybozu::severity::info;
    std::string m_logfile;
    unsigned int m_buckets = DEFAULT_BUCKETS;
    std::size_t m_max_data_size = DEFAULT_MAX_DATA_SIZE;
    std::size_t m_heap_data_limit = DEFAULT_HEAP_DATA_LIMIT;
    std::size_t m_memory_limit = DEFAULT_MEMORY_LIMIT;
    unsigned int m_repl_bufsize = DEFAULT_REPL_BUFSIZE;
    uint64_t m_initial_repl_sleep_delay_usec = DEFAULT_INITIAL_REPL_SLEEP_DELAY_USEC;
    bool m_secure_erase = false;
    bool m_lock_memory = false;
    unsigned int m_workers = DEFAULT_WORKER_THREADS;
    unsigned int m_gc_interval = DEFAULT_GC_INTERVAL;
    unsigned int m_slave_timeout = DEFAULT_SLAVE_TIMEOUT;
    counter_config m_counter_config;
};

// Global configuration object.
extern config g_config;

} // namespace yrmcds

#endif // YRMCDS_CONFIG_HPP
