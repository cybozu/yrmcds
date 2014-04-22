// (C) 2014 Cybozu.

#include "gc.hpp"
#include "stats.hpp"

namespace yrmcds { namespace semaphore {

void gc_thread::run() {
    using namespace std::chrono;
    auto t1 = steady_clock::now();

    gc();
    g_stats.objects.store(m_objects, std::memory_order_relaxed);
    g_stats.used_memory.store(m_used_memory, std::memory_order_relaxed);
    g_stats.conflicts.store(m_conflicts, std::memory_order_relaxed);
    g_stats.gc_count.fetch_add(1, std::memory_order_relaxed);

    auto t2 = steady_clock::now();
    std::uint64_t us = (std::uint64_t)duration_cast<microseconds>(t2-t1).count();
    g_stats.last_gc_elapsed.store(us, std::memory_order_relaxed);
    g_stats.total_gc_elapsed.fetch_add(us, std::memory_order_relaxed);

    cybozu::logger::debug() << "Semaphore GC end: elapsed=" << us
                            << "us, survived=" << m_objects;
}

void gc_thread::gc() {
    auto pred = [this](const cybozu::hash_key& k, object& obj) -> bool {
        if( obj.deletable() )
            return true;
        ++m_objects;
        ++m_objects_in_bucket;
        if( m_objects_in_bucket == 2 )
            ++m_conflicts;
        m_used_memory += sizeof(cybozu::hash_key) + k.length() + sizeof(object);
        return false;
    };

    for( auto it = m_hash.begin(); it != m_hash.end(); ++it ) {
        m_objects_in_bucket = 0;
        it->gc(pred);
    }
}

}} // namespace yrmcds::semaphore
