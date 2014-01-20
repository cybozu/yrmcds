// Semaphore protocol logics and data structures
// (C) 2014 Cybozu.

#ifndef YRMCDS_SEMAPHORE_HANDLER_HPP
#define YRMCDS_SEMAPHORE_HANDLER_HPP

#include "../handler.hpp"
#include "gc.hpp"
#include "object.hpp"

#include <cybozu/worker.hpp>
#include <cybozu/reactor.hpp>
#include <cybozu/tcp.hpp>

#include <functional>

namespace yrmcds { namespace semaphore {

class semaphore_handler: public protocol_handler {
public:
    semaphore_handler(const std::function<cybozu::worker*()>& m_finder,
                      const std::function<bool()>& is_slave,
                      cybozu::reactor& reactor);
    virtual void on_start() override;
    virtual void on_master_interval() override;
    virtual void on_master_end() override;
    virtual void on_clear() override;

private:
    bool gc_ready();
    std::unique_ptr<cybozu::tcp_socket> make_semaphore_socket(int s);

    std::function<cybozu::worker*()> m_finder;
    std::function<bool()> m_is_slave;
    cybozu::reactor& m_reactor;
    cybozu::hash_map<object> m_hash;
    std::time_t m_last_gc = 0;
    std::unique_ptr<gc_thread> m_gc_thread = nullptr;
};

}} // namespace yrmcds::memcache

#endif // YRMCDS_SEMAPHORE_HANDLER_HPP

