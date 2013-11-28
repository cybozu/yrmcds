// The server configurations.
// (C) 2013 Cybozu.

#ifndef YRMCDS_CONFIG_HPP
#define YRMCDS_CONFIG_HPP

#include "constants.hpp"

#include <cybozu/ip_address.hpp>
#include <cybozu/logger.hpp>

#include <stdexcept>
#include <cstdint>

namespace yrmcds {

// Configurations for yrmcds.
//
// Configurations for yrmcds.
// Defaults are configured properly without calling <config::load>.
class config {
public:
    // Setup default configurations.
    config(): m_vip("127.0.0.1"), m_tempdir(DEFAULT_TMPDIR) {
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

    const cybozu::ip_address& vip() const noexcept {
        return m_vip;
    }
    std::uint16_t port() const noexcept {
        return m_port;
    }
    std::uint16_t repl_port() const noexcept {
        return m_repl_port;
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
    const cybozu::severity threshold() const noexcept {
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
    unsigned int workers() const noexcept {
        return m_workers;
    }
    unsigned int gc_interval() const noexcept {
        return m_gc_interval;
    }

    void set_heap_data_limit(std::size_t new_limit) noexcept {
        m_heap_data_limit = new_limit;
    }

private:
    alignas(CACHELINE_SIZE)
    cybozu::ip_address m_vip;
    std::uint16_t m_port = DEFAULT_MEMCACHE_PORT;
    std::uint16_t m_repl_port = DEFAULT_REPL_PORT;
    unsigned int m_max_connections = 0;
    std::string m_tempdir;
    std::string m_user;
    std::string m_group;
    cybozu::severity m_threshold = cybozu::severity::info;
    std::string m_logfile;
    unsigned int m_buckets = DEFAULT_BUCKETS;
    std::size_t m_max_data_size = DEFAULT_MAX_DATA_SIZE;
    std::size_t m_heap_data_limit = DEFAULT_HEAP_DATA_LIMIT;
    std::size_t m_memory_limit = DEFAULT_MEMORY_LIMIT;
    unsigned int m_workers = DEFAULT_WORKER_THREADS;
    unsigned int m_gc_interval = DEFAULT_GC_INTERVAL;
};

// Global configuration object.
extern config g_config;

} // namespace yrmcds

#endif // YRMCDS_CONFIG_HPP
