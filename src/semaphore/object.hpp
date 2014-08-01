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
    explicit object(std::uint32_t consumption):
        m_consumption(consumption),
        m_max_consumption(consumption),
        m_last_updated(now()) {
        g_stats.total_objects.fetch_add(1, std::memory_order_relaxed);
    }

    bool acquire(std::uint32_t resources, std::uint32_t maximum) noexcept {
        if( m_consumption + resources > maximum )
            return false;

        std::time_t t = now();
        if( m_last_updated < nearest_boundary(t) )
            m_max_consumption = m_consumption + resources;
        else
            m_max_consumption = std::max(m_max_consumption, m_consumption + resources);
        m_last_updated = t;

        m_consumption += resources;
        return true;
    }

    bool release(std::uint32_t resources) noexcept {
        if( resources > m_consumption )
            return false;

        std::time_t t = now();
        if( m_last_updated < nearest_boundary(t) )
            m_max_consumption = m_consumption;
        m_last_updated = t;

        m_consumption -= resources;
        return true;
    }

    std::uint32_t consumption() const noexcept {
        return m_consumption;
    }

    std::uint32_t max_consumption() const noexcept {
        if( m_last_updated < nearest_boundary(now()) )
            return m_consumption;
        return m_max_consumption;
    }

    bool deletable() const noexcept {
        return m_consumption == 0 && m_last_updated < nearest_boundary(now());
    }

private:
    static std::time_t now() {
        return g_current_time.load(std::memory_order_relaxed);
    }

    static std::time_t nearest_boundary(std::time_t t) {
        unsigned int interval = g_config.semaphore().consumption_stats_interval();
        return (t / interval) * interval;
    }

    std::uint32_t m_consumption;
    std::uint32_t m_max_consumption;
    std::time_t m_last_updated;
};

}} // namespace yrmcds::semaphore

#endif // YRMCDS_SEMAPHORE_OBJECT_HPP
