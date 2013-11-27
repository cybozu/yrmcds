// Worker synchronizer.
// (C) 2013 Cybozu.

#ifndef YRMCDS_SYNC_HPP
#define YRMCDS_SYNC_HPP

#include "constants.hpp"

#include <cybozu/worker.hpp>

#include <bitset>
#include <functional>
#include <memory>
#include <vector>

namespace yrmcds {

class sync_request final {
public:
    explicit sync_request(std::function<void()> callback):
        m_callback(callback) {}

    void set(int n) { m_flags[n] = true; }

    bool check() {
        if( ! m_flags.all() ) return false;
        m_callback();
        return true;
    }

private:
    std::function<void()> m_callback;
    std::bitset<MAX_WORKERS> m_flags;
};


class syncer {
public:
    explicit syncer(const std::vector<std::unique_ptr<cybozu::worker>>& workers):
        m_workers(workers) {}

    bool empty() const noexcept {
        return m_requests.empty();
    }

    void add_request(std::unique_ptr<sync_request> req) {
        const std::size_t n = m_workers.size();
        for( std::size_t i = 0; i < n; ++i ) {
            if( ! m_workers[i]->is_running() )
                req->set(i);
        }
        for( std::size_t i = n; i < MAX_WORKERS; ++i )
            req->set(i);
        if( ! req->check() )
            m_requests.emplace_back( std::move(req) );
    }

    void check() {
        const std::size_t n = m_workers.size();
        for( std::size_t i = 0; i < n; ++i ) {
            if( ! m_workers[i]->is_running() ) {
                for( auto& req: m_requests )
                    req->set(i);
            }
        }
        for( auto it = m_requests.begin(); it != m_requests.end(); ) {
            if( (*it)->check() ) {
                it = m_requests.erase(it);
            } else {
                ++it;
            }
        }
    }

private:
    const std::vector<std::unique_ptr<cybozu::worker>>& m_workers;
    std::vector<std::unique_ptr<sync_request>> m_requests;
};

} // namespace yrmcds

#endif // YRMCDS_SYNC_HPP
