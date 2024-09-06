// Constants used in yrmcds.
// (C) 2013-2015 Cybozu.

#ifndef YRMCDS_CONSTANTS_HPP
#define YRMCDS_CONSTANTS_HPP

#include <cstddef>
#include <cstdint>

namespace yrmcds {

const std::uint16_t DEFAULT_MEMCACHE_PORT  = 11211;
const std::uint16_t DEFAULT_REPL_PORT      = 11213;
const std::uint16_t DEFAULT_COUNTER_PORT = 11215;
const unsigned int  DEFAULT_BUCKETS        = 1000000;
const std::size_t   DEFAULT_MAX_DATA_SIZE  = static_cast<std::size_t>(1) << 20;
const std::size_t   DEFAULT_HEAP_DATA_LIMIT= 256 << 10;
const std::size_t   DEFAULT_MEMORY_LIMIT   = static_cast<std::size_t>(1) << 30;
const unsigned int  DEFAULT_REPL_BUFSIZE   = 30;
const std::uint64_t DEFAULT_INITIAL_REPL_SLEEP_DELAY_USEC = 0;
const int           DEFAULT_WORKER_THREADS = 8;
const unsigned int  DEFAULT_GC_INTERVAL    = 10;
const unsigned int  DEFAULT_SLAVE_TIMEOUT  = 10;
const char          DEFAULT_TMPDIR[]       = "/var/tmp";
const unsigned int  DEFAULT_STAT_INTERVAL  = 86400;

const std::size_t   MAX_KEY_LENGTH      = 250; // 250 bytes
const int           MASTER_CHECKS       = 50; // wait 50 * 100ms = 5 seconds
const int           FLUSH_AGE           = 10;
const std::size_t   MAX_RECVSIZE        = 2 << 20; // 2 MiB
const std::size_t   WORKER_BUFSIZE      = 5 << 20; // 5 MiB
const int           MAX_WORKERS         = 64;
const std::size_t   MAX_REQUEST_LENGTH  = 30 << 20; // 30 MiB
const int           MAX_SLAVES          = 5;
const int           MAX_CONSECUTIVE_GCS = 3;

const char          VERSION[] = "yrmcds version 1.1.12";

} // namespace yrmcds

// Define CACHELINE_SIZE if it is not defined to prevent IDE from throwing an error
// The actual value used is written in Makefile.
#ifndef CACHELINE_SIZE
#define CACHELINE_SIZE 32
#endif

#endif // YRMCDS_CONSTANTS_HPP
