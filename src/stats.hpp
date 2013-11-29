// Atomic counters for statistics.
// (C) 2013 Cybozu.

#ifndef YRMCDS_STATS_HPP
#define YRMCDS_STATS_HPP

#include "memcache.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <ctime>

namespace yrmcds {

// statistics counters.
struct statistics {
    statistics(): current_time(std::time(nullptr)) {}

    // Reset all counters.
    void reset() noexcept;

    /* object counters.  Updated at every GC. */
    alignas(CACHELINE_SIZE)
    std::atomic<std::uint32_t> objects;
    std::atomic<std::uint32_t> objects_under_1k;
    std::atomic<std::uint32_t> objects_under_4k;
    std::atomic<std::uint32_t> objects_under_16k;
    std::atomic<std::uint32_t> objects_under_64k;
    std::atomic<std::uint32_t> objects_under_256k;
    std::atomic<std::uint32_t> objects_under_1m;
    std::atomic<std::uint32_t> objects_under_4m;
    std::atomic<std::uint32_t> objects_huge;

    /* bucket statistics.  Updated at every GC. */
    std::atomic<std::size_t> used_memory;
    std::atomic<std::uint32_t> conflicts;

    /* GC statistics. Updated at every GC, of course. */
    std::atomic<std::uint32_t> gc_count;
    std::atomic<std::uint32_t> oldest_age;
    std::atomic<std::size_t> largest_object_size;
    std::atomic<std::uint32_t> last_expirations;
    std::atomic<std::uint32_t> last_evictions;
    std::atomic<std::uint64_t> total_evictions;
    std::atomic<std::uint64_t> last_gc_elapsed;  // micro seconds
    std::atomic<std::uint64_t> total_gc_elapsed; // micro seconds

    /* Realtime staticstics. */
    alignas(CACHELINE_SIZE)
    std::atomic<std::uint64_t> total_objects;
    alignas(CACHELINE_SIZE)
    std::atomic<std::time_t> current_time;
    alignas(CACHELINE_SIZE)
    std::atomic<std::time_t> flush_time; // abused by "flush_all"
    alignas(CACHELINE_SIZE)
    std::atomic<std::uint64_t> curr_connections;
    std::atomic<std::uint64_t> total_connections;
    alignas(CACHELINE_SIZE)
    std::atomic<std::uint64_t> text_ops[(std::size_t)memcache::text_command::END_OF_COMMAND];
    std::atomic<std::uint64_t> bin_ops[(std::size_t)memcache::binary_command::END_OF_COMMAND];
};

extern statistics g_stats;

} // namespace yrmcds

#endif // YRMCDS_STATS_HPP
