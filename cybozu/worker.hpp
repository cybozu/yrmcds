// General worker thread.
// (C) 2013 Cybozu.

#ifndef CYBOZU_WORKER_HPP
#define CYBOZU_WORKER_HPP

#include <cybozu/dynbuf.hpp>
#include <cybozu/thread.hpp>
#include <cybozu/util.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <sys/eventfd.h>
#include <unistd.h>

namespace cybozu {

// General worker thread implementation.
//
// Every worker thread has a pre-allocated internal memory buffer.
// A worker receives a new `job` through <post_job> as a callback function,
// then invokes the function with that buffer.
//
// To stop the worker, call <stop>.
class worker final: public thread_base<worker> {
public:
    // Constructor.
    // @bufsiz   The size of the internal buffer.
    worker(std::size_t bufsiz)
        : m_running(false), m_exit(false),
          m_event(eventfd(0, EFD_CLOEXEC)), m_buffer(bufsiz)
    {
        if( m_event == -1 )
            throw_unix_error(errno, "eventfd");
    }
    ~worker() {
        ::close(m_event);
    }

    // forbid copy & assignment
    worker(const worker&) = delete;
    worker& operator=(const worker&) = delete;
    worker(worker&&) = delete;
    worker& operator=(worker&&) = delete;

    // Jobs for workers are this kind of functions.
    // `buf` is a memory buffer that can be used freely for temporary data.
    using job = std::function<void(dynbuf& buf)>;

    // Return `true` while this worker thread is busy for a job.
    bool is_running() const noexcept {
        return m_running.load(std::memory_order_acquire);
    }

    // Ask this worker thread to execute a new job.
    // @job_  A callback function to be executed by the worker thread.
    void post_job(job job_) {
        m_job = job_;
        m_running.store(true, std::memory_order_release);
        notify();
    }

    // Stop this worker thread.
    //
    // The thread will be joined automatically.
    void stop() {
        m_exit = true;
        m_running.store(true, std::memory_order_release);
        notify();
        m_thread.join();
    }

    // CRTP method for <thread_base>.
    void run() {
        while ( true ) {
            // wait a new job or an exit signal
            m_running.store(false, std::memory_order_release);
            std::uint64_t i;
            ssize_t n = ::read(m_event, &i, sizeof(i));
            if( n == -1 )
                throw_unix_error(errno, "read(eventfd)");
            while( ! m_running.load(std::memory_order_acquire) );

            if( m_exit ) return;

            m_job(m_buffer);
            m_buffer.reset();
        }
    }

private:
    alignas(CACHELINE_SIZE)
    std::atomic<bool> m_running;
    bool m_exit;
    const int m_event;
    dynbuf m_buffer;
    job m_job;

    void notify() {
        std::uint64_t i = 1;
        ssize_t n = ::write(m_event, &i, sizeof(i));
        if( n == -1 )
            throw_unix_error(errno, "write(eventfd)");
    }
};

} // namespace cybozu

#endif // CYBOZU_WORKER_HPP
