// (C) 2013 Cybozu.

#include "config.hpp"
#include <cybozu/config_parser.hpp>
#include <cybozu/filesystem.hpp>
#include <cybozu/logger.hpp>

#include <unordered_map>

namespace {

const char VIRTUAL_IP[] = "virtual_ip";
const char PORT[] = "port";
const char REPL_PORT[] = "repl_port";
const char MAX_CONNECTIONS[] = "max_connections";
const char TEMP_DIR[] = "temp_dir";
const char USER[] = "user";
const char GROUP[] = "group";
const char LOG_THRESHOLD[] = "log.threshold";
const char LOG_FILE[] = "log.file";
const char BUCKETS[] = "buckets";
const char MAX_DATA_SIZE[] = "max_data_size";
const char HEAP_DATA_LIMIT[] = "heap_data_limit";
const char MEMORY_LIMIT[] = "memory_limit";
const char WORKERS[] = "workers";
const char GC_INTERVAL[] = "gc_interval";

std::unordered_map<std::string, cybozu::severity> THRESHOLDS {
    {"error", cybozu::severity::error},
    {"warning", cybozu::severity::warning},
    {"info", cybozu::severity::info},
    {"debug", cybozu::severity::debug}
};

inline std::size_t parse_unit(std::string& s, const char* cmd) {
    std::size_t base = 1;
    switch( s.back() ) {
    case 'k':
    case 'K':
        base <<= 10;
        break;
    case 'm':
    case 'M':
        base <<=20;
        break;
    case 'g':
    case 'G':
        base <<=30;
        break;
    }
    if( base != 1 )
        s.pop_back();
    int n = std::stoi( s );
    if( n < 1 )
        throw yrmcds::config::bad_config(cmd + std::string(" must be > 0"));
    return base * static_cast<std::size_t>(n);
}

} // anonymous namespace

namespace yrmcds {

void config::load(const std::string& path) {
    cybozu::config_parser cp(path);

    if( cp.exists(VIRTUAL_IP) )
        m_vip.parse(cp.get(VIRTUAL_IP));

    if( cp.exists(PORT) ) {
        int n = cp.get_as_int(PORT);
        if( n < 1 || n > 65535 )
            throw bad_config("Bad port: " + cp.get(PORT));
        m_port = static_cast<std::uint16_t>(n);
    }

    if( cp.exists(REPL_PORT) ) {
        int n = cp.get_as_int(REPL_PORT);
        if( n < 1 || n > 65535 )
            throw bad_config("Bad repl port: " + cp.get(REPL_PORT));
        m_repl_port = static_cast<std::uint16_t>(n);
    }

    if( cp.exists(MAX_CONNECTIONS) ) {
        int conns = cp.get_as_int(MAX_CONNECTIONS);
        if( conns < 0 )
            throw bad_config("max_connections must be >= 0");
        m_max_connections = conns;
    }

    if( cp.exists(TEMP_DIR) ) {
        m_tempdir = cp.get(TEMP_DIR);
        if( ! cybozu::is_dir(m_tempdir) )
            throw bad_config("Not a directory: " + m_tempdir);
        if( ! cybozu::is_writable(m_tempdir) )
            throw bad_config("Directory not writable: " + m_tempdir);
    }

    if( cp.exists(USER) ) {
        m_user = cp.get(USER);
    }

    if( cp.exists(GROUP) ) {
        m_group = cp.get(GROUP);
    }

    if( cp.exists(LOG_THRESHOLD) ) {
        auto it = THRESHOLDS.find(cp.get(LOG_THRESHOLD));
        if( it == THRESHOLDS.end() )
            throw bad_config("Invalid threshold: " + cp.get(LOG_THRESHOLD));
        m_threshold = it->second;
    }

    if( cp.exists(LOG_FILE) ) {
        m_logfile = cp.get(LOG_FILE);
        if( m_logfile.size() == 0 || m_logfile[0] != '/' )
            throw bad_config("Invalid log file: " + m_logfile);
    }

    if( cp.exists(BUCKETS) ) {
        int buckets = cp.get_as_int(BUCKETS);
        if( buckets < 1 )
            throw bad_config("buckets must be > 0");
        if( buckets < 10000 )
            cybozu::logger::warning() << "Too small bucket count!";
        m_buckets = buckets;
    }

    if( cp.exists(MAX_DATA_SIZE) ) {
        std::string t = cp.get(MAX_DATA_SIZE);
        if( t.empty() )
            throw bad_config("max_data_size must not be empty");
        m_max_data_size = parse_unit(t, MAX_DATA_SIZE);
    }

    if( cp.exists(HEAP_DATA_LIMIT) ) {
        std::string t = cp.get(HEAP_DATA_LIMIT);
        if( t.empty() )
            throw bad_config("heap_data_limit must not be empty");
        m_heap_data_limit = parse_unit(t, HEAP_DATA_LIMIT);
        if( m_heap_data_limit < 4096 )
            throw bad_config("too small heap_data_limit");
    }

    if( cp.exists(MEMORY_LIMIT) ) {
        std::string t = cp.get(MEMORY_LIMIT);
        if( t.empty() )
            throw bad_config("memory_limit must not be empty");
        m_memory_limit = parse_unit(t, MEMORY_LIMIT);
    }

    if( cp.exists(WORKERS) ) {
        int n = cp.get_as_int(WORKERS);
        if( n < 1 )
            throw bad_config("workers must be > 0");
        if( n > MAX_WORKERS )
            throw bad_config("workers must be <= " +
                             std::to_string(MAX_WORKERS));
        m_workers = n;
    }

    if( cp.exists(GC_INTERVAL) ) {
        int n = cp.get_as_int(GC_INTERVAL);
        if( n < 1 )
            throw bad_config("gc_interval must be > 0");
        m_gc_interval = n;
    }
}

config g_config;

} // namespace yrmcds
