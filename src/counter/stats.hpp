// Atomic counters for statistics.
// (C) 2014 Cybozu.

#ifndef YRMCDS_COUNTER_STATS_HPP
#define YRMCDS_COUNTER_STATS_HPP

#include "counter.hpp"
#include "../constants.hpp"

#include <atomic>
#include <cstdint>

namespace yrmcds { namespace counter {

struct statistics {
    statistics() {
        reset();
    }

    void reset() noexcept;

    alignas(CACHELINE_SIZE)
    std::atomic<std::uint32_t> objects;
    std::atomic<std::uint64_t> used_memory;
    std::atomic<std::uint32_t> conflicts;
    std::atomic<std::uint32_t> gc_count;
    std::atomic<std::uint64_t> last_gc_elapsed;  // micro seconds
    std::atomic<std::uint64_t> total_gc_elapsed; // micro seconds

    alignas(CACHELINE_SIZE)
    std::atomic<std::uint64_t> total_objects;
    alignas(CACHELINE_SIZE)
    std::atomic<std::uint64_t> curr_connections;
    std::atomic<std::uint64_t> total_connections;
    alignas(CACHELINE_SIZE)
    std::atomic<std::uint64_t> ops[(std::size_t)command::END_OF_COMMAND];
};

extern statistics g_stats;

}} // namespace yrmcds::counter

#endif // YRMCDS_COUNTER_STATS_HPP
