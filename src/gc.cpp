// (C) 2013 Cybozu.

#include "config.hpp"
#include "gc.hpp"
#include "replication.hpp"
#include "stats.hpp"

#include <algorithm>
#include <cstdlib>

namespace yrmcds {

void gc_thread::run() {
    if( ! cybozu::has_ip_address(g_config.vip()) ) {
        cybozu::logger::error() << "VIP has been lost.  Exiting quickly...";
        std::quick_exit(2);
    }

    using namespace std::chrono;
    auto t1 = steady_clock::now();
    gc();
    if( ! m_new_slaves.empty() )
        cybozu::logger::info() << "Initial replication completed for "
                               << m_new_slaves.size() << " new slave(s).";
    g_stats.objects.store(m_objects, std::memory_order_relaxed);
    g_stats.objects_under_1k.store(m_objects_under_1k, std::memory_order_relaxed);
    g_stats.objects_under_4k.store(m_objects_under_4k, std::memory_order_relaxed);
    g_stats.objects_under_16k.store(m_objects_under_16k, std::memory_order_relaxed);
    g_stats.objects_under_64k.store(m_objects_under_64k, std::memory_order_relaxed);
    g_stats.objects_under_256k.store(m_objects_under_256k, std::memory_order_relaxed);
    g_stats.objects_under_1m.store(m_objects_under_1m, std::memory_order_relaxed);
    g_stats.objects_under_4m.store(m_objects_under_4m, std::memory_order_relaxed);
    g_stats.objects_huge.store(m_objects_huge, std::memory_order_relaxed);
    g_stats.used_memory.store(m_used_memory, std::memory_order_relaxed);
    g_stats.conflicts.store(m_conflicts, std::memory_order_relaxed);
    g_stats.gc_count.fetch_add(1, std::memory_order_relaxed);
    g_stats.oldest_age.store(m_oldest_age, std::memory_order_relaxed);
    g_stats.largest_object_size.store(m_largest_object_size, std::memory_order_relaxed);
    g_stats.last_expirations.store(m_last_expirations, std::memory_order_relaxed);
    g_stats.last_evictions.store(m_last_evictions, std::memory_order_relaxed);
    g_stats.total_evictions.fetch_add(m_last_evictions, std::memory_order_relaxed);

    auto t2 = steady_clock::now();
    std::uint64_t us = static_cast<std::uint64_t>(
        duration_cast<microseconds>(t2-t1).count() );
    g_stats.last_gc_elapsed.store(us, std::memory_order_relaxed);
    g_stats.total_gc_elapsed.fetch_add(us, std::memory_order_relaxed);
    cybozu::logger::debug() << "GC end: elapsed=" << us
                            << "us, expired=" << m_last_expirations
                            << ", evicted=" << m_last_evictions
                            << ", survived=" << m_objects;
}

void gc_thread::gc() {
    std::time_t t = g_stats.flush_time.load();
    bool flush = (t != 0) && (std::time(nullptr) >= t);
    unsigned int evict_age = 0;
    if( (! flush) &&
        (g_stats.used_memory.load(std::memory_order_relaxed) >
         g_config.memory_limit()) ) {
        unsigned int oldest_age =
            g_stats.oldest_age.load(std::memory_order_relaxed);
        unsigned int one_hour = 3600U / g_config.gc_interval() + 1;
        if( oldest_age < (one_hour * 2) ) {
            evict_age = std::max(1U, oldest_age / 2);
        } else {
            evict_age = oldest_age - one_hour;
        }
        cybozu::logger::warning() << "Evicting object of "
                                  << evict_age << " gc old";
    }

    auto pred =
        [this,flush,evict_age](const cybozu::hash_key& k, object& obj) ->bool {
        if( flush && (! obj.locked()) ) {
            if( ! m_slaves.empty() )
                repl_delete(m_slaves, k);
            return true;
        }
        if( evict_age > 0 && obj.age() >= evict_age && (! obj.locked()) ) {
            ++ m_last_evictions;
            if( ! m_slaves.empty() )
                repl_delete(m_slaves, k);
            return true;
        }
        if( obj.expired() ) {
            ++ m_last_expirations;
            if( ! m_slaves.empty() )
                repl_delete(m_slaves, k);
            return true;
        }

        obj.survive(m_flushers);
        if( ++m_objects_in_bucket == 2 )
            ++ m_conflicts;
        ++ m_objects;
        std::size_t size = obj.size();
        if( size < 1024 ) {
            ++ m_objects_under_1k;
        } else if( size < 4096 ) {
            ++ m_objects_under_4k;
        } else if( size < (16 <<10) ) {
            ++ m_objects_under_16k;
        } else if( size < (64 <<10) ) {
            ++ m_objects_under_64k;
        } else if( size < (256 <<10) ) {
            ++ m_objects_under_256k;
        } else if( size < (1 <<20) ) {
            ++ m_objects_under_1m;
        } else if( size < (4 <<20) ) {
            ++ m_objects_under_4m;
        } else {
            ++ m_objects_huge;
        }
        m_used_memory += sizeof(cybozu::hash_key) + k.length() + sizeof(object);
        if( g_config.heap_data_limit() >= obj.size() )
            m_used_memory += obj.size();
        m_oldest_age = std::max(m_oldest_age, obj.age());
        m_largest_object_size = std::max(m_largest_object_size, obj.size());
        if( ! m_new_slaves.empty() )
            repl_object(m_new_slaves, k, obj, false);
        return false;
    };

    for( auto it = m_hash.begin(); it != m_hash.end(); ++it ) {
        m_objects_in_bucket = 0;
        it->gc(pred);
        m_flushers.clear();
    }

    if( flush )
        g_stats.flush_time.store(0);
}


} // namespace yrmcds
