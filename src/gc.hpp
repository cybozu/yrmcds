// Garbage object collection.
// (C) 2013 Cybozu.

#ifndef YRMCDS_GC_HPP
#define YRMCDS_GC_HPP

#include "object.hpp"
#include "stats.hpp"

#include <cybozu/hash_map.hpp>
#include <cybozu/logger.hpp>
#include <cybozu/tcp.hpp>
#include <cybozu/thread.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace yrmcds {

class gc_thread final: public cybozu::thread_base<gc_thread> {
public:
    gc_thread(cybozu::hash_map<object>& m,
              const std::vector<cybozu::tcp_socket*>& slaves,
              const std::vector<cybozu::tcp_socket*>& new_slaves):
        m_hash(m), m_slaves(slaves), m_new_slaves(new_slaves) {
        for( cybozu::tcp_socket* s: new_slaves ) {
            if( std::find(slaves.begin(), slaves.end(), s) == slaves.end() )
                m_slaves.push_back(s);
        }
        m_flushers.reserve(10);
    }
    gc_thread(const gc_thread&) = delete;
    gc_thread& operator=(const gc_thread&) = delete;
    gc_thread(gc_thread&&) = delete;
    gc_thread& operator=(gc_thread&&) = delete;
    ~gc_thread() { m_thread.join(); }

    void run();

private:
    void gc();

    cybozu::hash_map<object>& m_hash;
    std::vector<cybozu::tcp_socket*> m_slaves;
    std::vector<cybozu::tcp_socket*> m_new_slaves;
    std::uint32_t m_objects = 0;
    std::uint32_t m_objects_under_1k = 0;
    std::uint32_t m_objects_under_4k = 0;
    std::uint32_t m_objects_under_16k = 0;
    std::uint32_t m_objects_under_64k = 0;
    std::uint32_t m_objects_under_256k = 0;
    std::uint32_t m_objects_under_1m = 0;
    std::uint32_t m_objects_under_4m = 0;
    std::uint32_t m_objects_huge = 0;
    std::size_t   m_used_memory = 0;
    std::uint32_t m_conflicts = 0;
    std::uint32_t m_oldest_age = 0;
    std::size_t   m_largest_object_size = 0;
    std::uint32_t m_last_expirations = 0;
    std::uint32_t m_last_evictions = 0;

    int m_objects_in_bucket = 0;
    std::vector<file_flusher> m_flushers;
};

} // namespace yrmcds

#endif // YRMCDS_GC_HPP
