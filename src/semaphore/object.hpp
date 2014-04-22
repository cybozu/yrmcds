// The semaphore object.
// (C) 2014 Cybozu.

#ifndef YRMCDS_SEMAPHORE_OBJECT_HPP
#define YRMCDS_SEMAPHORE_OBJECT_HPP

#include "../config.hpp"
#include "../global.hpp"
#include "stats.hpp"

#include <algorithm>
#include <cstdint>

namespace yrmcds { namespace semaphore {

class object {
public:
    object(std::uint32_t available, std::uint32_t initial):
        m_available(available),
        m_maximum(initial),
        m_min_available(available),
        m_last_updated(now()) {
        g_stats.total_objects.fetch_add(1, std::memory_order_relaxed);
    }

    bool acquire(std::uint32_t resources) noexcept {
        std::time_t t = now();
        if( m_last_updated < nearest_boundary(t) )
            m_min_available = m_available - resources;
        else
            m_min_available = std::min(m_min_available, m_available - resources);
        m_last_updated = t;

        if( m_available < resources )
            return false;
        m_available -= resources;
        return true;
    }

    bool release(std::uint32_t resources) noexcept {
        std::time_t t = now();
        if( m_last_updated < nearest_boundary(t) )
            m_min_available = m_available;
        m_last_updated = t;

        if( m_available + resources > m_maximum )
            return false;
        m_available += resources;
        return true;
    }

    std::uint32_t available() const noexcept {
        return m_available;
    }

    std::uint32_t maximum() const noexcept {
        return m_maximum;
    }

    std::uint32_t max_consumption() const noexcept {
        if( m_last_updated < nearest_boundary(now()) )
            return m_maximum - m_available;
        return m_maximum - m_min_available;
    }

    bool deletable() const noexcept {
        if( m_available != m_maximum )
            return false;
        return m_last_updated < nearest_boundary(now());
    }

private:
    static std::time_t now() {
        return g_current_time.load(std::memory_order_relaxed);
    }

    static std::time_t nearest_boundary(std::time_t t) {
        unsigned int interval = g_config.semaphore().consumption_stats_interval();
        return (t / interval) * interval;
    }

    std::uint32_t m_available;
    std::uint32_t m_maximum;
    std::uint32_t m_min_available;
    std::time_t m_last_updated;
};

}} // namespace yrmcds::semaphore

#endif // YRMCDS_SEMAPHORE_OBJECT_HPP
