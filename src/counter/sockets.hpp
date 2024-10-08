// Defines counter sockets for yrmcds.
// (C) 2014 Cybozu.

#ifndef YRMCDS_COUNTER_SOCKETS_HPP
#define YRMCDS_COUNTER_SOCKETS_HPP

#include "../constants.hpp"
#include "object.hpp"
#include "counter.hpp"
#include "stats.hpp"

#include <cybozu/dynbuf.hpp>
#include <cybozu/hash_map.hpp>
#include <cybozu/tcp.hpp>
#include <cybozu/util.hpp>
#include <cybozu/worker.hpp>

#include <functional>
#include <unordered_map>
#include <vector>

namespace yrmcds { namespace counter {

class counter_socket: public cybozu::tcp_socket {
public:
    counter_socket(int fd,
                     const std::function<cybozu::worker*()>& finder,
                     cybozu::hash_map<object>& hash);
    virtual ~counter_socket();

    void execute(const counter::request& cmd);

private:
    alignas(CACHELINE_SIZE)
    std::atomic<bool> m_busy;
    const std::function<cybozu::worker*()>& m_finder;
    cybozu::hash_map<object>& m_hash;
    cybozu::dynbuf m_pending;
    std::function<void(cybozu::dynbuf&)> m_recvjob;
    std::function<void(cybozu::dynbuf&)> m_sendjob;

    std::unordered_map<const cybozu::hash_key*, std::uint32_t>
        m_acquired_resources;

    virtual void on_invalidate(int fd) override final {
        g_stats.curr_connections.fetch_sub(1);
        cybozu::tcp_socket::on_invalidate(fd);
    }

    bool on_readable(int) override;
    bool on_writable(int) override;

    void cmd_get(const counter::request& cmd, counter::response& r);
    void cmd_acquire(const counter::request& cmd, counter::response& r);
    void cmd_release(const counter::request& cmd, counter::response& r);
    void cmd_dump(counter::response& r);

    // `on_acquire` augments the number of resources this connection has acquired
    // recorded in `m_acquired_resources`.
    void on_acquire(const cybozu::hash_key& k, std::uint32_t resources);

    // `on_release` reduces the number of resources this connection has acquired
    // recorded in `m_acquired_resources`.
    // When this connection has not acquired the specified number of resources,
    // returns false. Otherwise, returns true.
    bool on_release(const cybozu::hash_key& k, std::uint32_t resources);

    void release_all();
};

}} // namespace yrmdcs::counter

#endif // YRMCDS_COUNTER_SOCKETS_HPP
