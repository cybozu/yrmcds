// (C) 2014 Cybozu.

#include "stats.hpp"

namespace yrmcds { namespace semaphore {

statistics g_stats;

void statistics::reset() noexcept {
    objects = 0;
    used_memory = 0;
    conflicts = 0;
    gc_count = 0;
    last_gc_elapsed = 0;
    total_gc_elapsed = 0;
    total_objects = 0;
    curr_connections = 0;
    total_connections = 0;
    for( auto& v: ops )
        v = 0;
}

}} // namespace yrmcds::semaphore
