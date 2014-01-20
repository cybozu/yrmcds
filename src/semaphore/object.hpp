// The semaphore object.
// (C) 2014 Cybozu.

#ifndef YRMCDS_SEMAPHORE_OBJECT_HPP
#define YRMCDS_SEMAPHORE_OBJECT_HPP

#include <cstdint>

namespace yrmcds { namespace semaphore {

class object {
public:
    object(std::uint32_t available, std::uint32_t initial);

    bool acquire(uint32_t resources) {
        if( m_available < resources )
            return false;
        m_available -= resources;
        return true;
    }

    bool release(uint32_t resources) {
        if( m_available + resources > m_initial )
            return false;
        m_available += resources;
        return true;
    }

    uint32_t available() const noexcept { return m_available; }
    bool acquired() const noexcept { return m_available != m_initial; }

private:
    uint32_t m_available;
    uint32_t m_initial;
};

}} // namespace yrmcds::semaphore

#endif // YRMCDS_SEMAPHORE_OBJECT_HPP
