// Garbage object collection.
// (C) 2014 Cybozu.

#ifndef YRMCDS_COUNTER_GC_HPP
#define YRMCDS_COUNTER_GC_HPP

#include "object.hpp"

#include <cybozu/thread.hpp>
#include <cybozu/hash_map.hpp>

namespace yrmcds { namespace counter {

class gc_thread final: public cybozu::thread_base<gc_thread> {
public:
    explicit gc_thread(cybozu::hash_map<object>& hash):
        m_hash(hash) {}

    gc_thread(const gc_thread&) = delete;
    gc_thread(gc_thread&&) = delete;
    gc_thread& operator=(const gc_thread&) = delete;
    gc_thread& operator=(gc_thread&&) = delete;

    ~gc_thread() { m_thread.join(); }

    void run();

private:
    void gc();

    cybozu::hash_map<object>& m_hash;

    std::uint32_t m_objects = 0;
    std::uint32_t m_conflicts = 0;
    std::uint64_t m_used_memory = 0;

    int m_objects_in_bucket = 0;
};

}} // namespace yrmcds::counter

#endif // YRMCDS_COUNTER_GC_HPP
