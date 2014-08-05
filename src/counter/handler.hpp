// Semaphore protocol logics and data structures
// (C) 2014 Cybozu.

#ifndef YRMCDS_COUNTER_HANDLER_HPP
#define YRMCDS_COUNTER_HANDLER_HPP

#include "../handler.hpp"
#include "gc.hpp"
#include "object.hpp"

#include <cybozu/reactor.hpp>
#include <cybozu/tcp.hpp>
#include <cybozu/worker.hpp>

#include <ctime>
#include <functional>

namespace yrmcds { namespace counter {

class handler: public protocol_handler {
public:
    handler(const std::function<cybozu::worker*()>& finder,
            cybozu::reactor& reactor);
    virtual void on_master_start() override;
    virtual void on_master_interval() override;
    virtual void on_master_end() override;
    virtual void dump_stats() override;
    virtual void clear() override;

private:
    bool gc_ready();
    std::unique_ptr<cybozu::tcp_socket> make_counter_socket(int s);

    std::function<cybozu::worker*()> m_finder;
    cybozu::reactor& m_reactor;
    cybozu::hash_map<object> m_hash;
    std::time_t m_last_gc = 0;
    std::unique_ptr<gc_thread> m_gc_thread = nullptr;
};

}} // namespace yrmcds::counter

#endif // YRMCDS_COUNTER_HANDLER_HPP
