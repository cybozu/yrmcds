// (C) 2014 Cybozu.

#include "object.hpp"
#include "stats.hpp"

namespace yrmcds { namespace semaphore {

object::object(uint32_t available, uint32_t initial):
    m_available(available), m_initial(initial) {
    g_stats.total_objects.fetch_add(1, std::memory_order_relaxed);
}

}} // namespace yrmcds::semaphore
