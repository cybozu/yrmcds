// Spinlock.
// (C) 2013 Cybozu.

#ifndef CYBOZU_SPINLOCK_HPP
#define CYBOZU_SPINLOCK_HPP

#include <atomic>
#if defined(__i386__) || defined(__x86_64__)
# include <x86intrin.h>
#endif

namespace cybozu {

// A simple spinlock.
class spinlock {
public:
    spinlock(): m_flag(ATOMIC_FLAG_INIT) {}
    void lock() {
        while( m_flag.test_and_set(std::memory_order_acquire) )
            pause();
    }
    void unlock() {
        m_flag.clear(std::memory_order_release);
    }

private:
    std::atomic_flag m_flag;
    void pause() {
#if defined(__i386__) || defined(__x86_64__)
# if defined(__SSE__)
        _mm_pause();
# else
        __asm__ __volatile__ ("rep; nop");
# endif
#endif
    }
};

} // namespace cybozu

#endif // CYBOZU_SPINLOCK_HPP
