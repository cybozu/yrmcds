// (C) 2013-2014 Cybozu.

#include "stats.hpp"

namespace yrmcds { namespace memcache {

statistics g_stats;

void statistics::reset() noexcept {
    objects = 0;
    objects_under_1k = 0;
    objects_under_4k = 0;
    objects_under_16k = 0;
    objects_under_64k = 0;
    objects_under_256k = 0;
    objects_under_1m = 0;
    objects_under_4m = 0;
    objects_huge = 0;

    /* bucket statistics.  Updated at every GC. */
    used_memory = 0;
    conflicts = 0;

    /* GC statistics. Updated at every GC, of course. */
    gc_count = 0;
    oldest_age = 0;
    largest_object_size = 0;
    last_expirations = 0;
    last_evictions = 0;
    total_evictions = 0;
    last_gc_elapsed = 0;
    total_gc_elapsed = 0;

    /* Replication statistics - non atomic. */
    repl_created = 0;
    repl_updated = 0;
    repl_removed = 0;

    /* Realtime staticstics. */
    total_objects = 0;
    flush_time = 0;
    curr_connections = 0;
    total_connections = 0;
    for( auto& v: text_ops )
        v = 0;
    for( auto& v: bin_ops )
        v = 0;
}

}} // namespace yrmcds::memcache
