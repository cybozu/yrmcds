// Worker thread and its data structures.
// (C) 2013 Cybozu.

#ifndef YRMCDS_WORKER_HPP
#define YRMCDS_WORKER_HPP

#include "memcache.hpp"
#include "object.hpp"
#include "sockets.hpp"

#include <cybozu/dynbuf.hpp>
#include <cybozu/hash_map.hpp>
#include <cybozu/tcp.hpp>
#include <cybozu/thread.hpp>
#include <cybozu/util.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <thread>
#include <unistd.h>

namespace yrmcds {

using slave_copier = std::function<std::vector<cybozu::tcp_socket*>()>;

// Worker thread and its data structures.
class worker final: public cybozu::thread_base<worker> {
public:
    worker(cybozu::hash_map<object>& m, slave_copier get_slaves);
    worker(const worker&) = delete;
    worker& operator=(const worker&) = delete;
    worker(worker&&) = delete;
    worker& operator=(worker&&) = delete;
    ~worker();

    void run();

    bool is_running() const noexcept {
        return m_running.load(std::memory_order_relaxed);
    }

    void wait() {
        m_running = false;
        std::uint64_t i;
        ssize_t n = ::read(m_event, &i, sizeof(i));
        if( n == -1 )
            cybozu::throw_unix_error(errno, "read(eventfd)");
        std::atomic_thread_fence(std::memory_order_acquire);
    }

    void post_job(memcache_socket* s) {
        m_running.store(true, std::memory_order_relaxed);
        m_socket = s;
        m_slaves = m_get_slaves();
        notify();
    }

    void stop() {
        m_exit.store(true, std::memory_order_relaxed);
        notify();
        m_thread.join();
    }

private:
    alignas(CACHELINE_SIZE)
    std::atomic<bool> m_running;
    std::atomic<bool> m_exit;
    cybozu::hash_map<object>& m_hash;
    slave_copier m_get_slaves;
    const int m_event;
    cybozu::dynbuf m_buffer;
    memcache_socket* m_socket = nullptr;
    std::vector<cybozu::tcp_socket*> m_slaves;

    void notify() {
        std::atomic_thread_fence(std::memory_order_release);
        std::uint64_t i = 1;
        ssize_t n = ::write(m_event, &i, sizeof(i));
        if( n == -1 )
            cybozu::throw_unix_error(errno, "write(eventfd)");
    }

    void exec_cmd_txt(const memcache::text_request& cmd);
    void exec_cmd_bin(const memcache::binary_request& cmd);
};

} // namespace yrmcds

#endif // YRMCDS_WORKER_HPP
