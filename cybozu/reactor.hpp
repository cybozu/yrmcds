// A reactor implementation using epoll(2).
// (C) 2013 Cybozu.

#ifndef CYBOZU_REACTOR_HPP
#define CYBOZU_REACTOR_HPP

#include "logger.hpp"
#include "spinlock.hpp"
#include "util.hpp"

#include <functional>
#include <memory>
#include <stdexcept>
#include <sys/epoll.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

namespace cybozu {

class reactor;

// An abstraction of a file descriptor.
//
// An abstraction of a file descriptor for use with <reactor>.
// The file descriptor should be set non-blocking.
//
// All member functions except for <invalidate_and_close> are for the
// reactor thread.  Sub classes can add methods for the other threads.
class resource {
public:
    // Constructor.
    // @fd     A UNIX file descriptor.
    explicit resource(int fd): m_fd(fd) {}
    resource(const resource&) = delete;
    resource& operator=(const resource&) = delete;

    // Close the file descriptor.
    virtual ~resource() {
        ::close(m_fd);
    }

    // Return the UNIX file descriptor for this resource.
    int fileno() const { return m_fd; }

    // `true` if this resource is still valid.
    bool valid() const {
        lock_guard g(m_lock);
        return m_valid;
    }

    // Invalidate this resource.
    //
    // This is for the reactor thread only.
    // You may call this from within <on_readable> or <on_writable>.
    bool invalidate() {
        lock_guard g(m_lock);
        if( ! m_valid ) return true;
        invalidate_( std::move(g) );
        return false;
    }

    // Invalidate this resource then request the reactor to remove this.
    //
    // This method invalidates this resource then requests the reactor
    // thread to remove this resource from the reactor.
    //
    // **Do not use this in the reactor thread**.
    void invalidate_and_close();

    // A template method called from within invalidate.
    //
    // A template method called from within invalidate.
    // Subclasses can override this to clean up something.
    virtual void on_invalidate() {}

    // Called when the reactor finds this resource is readable.
    //
    // This method is called when the reactor finds reading from this
    // resource will not block.
    //
    // `on_readable` is allowed to stop reading from `m_fd` before `EAGAIN`
    // or `EWOULDBLOCK` for fairness.  If you stop reading, make sure
    // to call `m_reactor->add_readable(*this)` from within `on_readable`.
    //
    // If some error happened, execute `return invalidate();`.
    //
    // `invalidate()` can be called when `recv` or `read` returns 0,
    // but it is up to you (or your application's protocol).  In general,
    // the client may still be waiting for the response.
    //
    // @return `true` or return value of <invalidate>.
    virtual bool on_readable() = 0;

    // Called when the reactor finds this resource is writable.
    //
    // This method is called when the reactor finds this resource gets
    // writable.  Unlike <on_readable>, `on_writable` must try to write
    // all pending data until it encounters `EAGAIN` or `EWOULDBLOCK`.
    //
    // If some error happened, execute `return invalidate();`.
    //
    // @return `true` or return value of <invalidate>.
    virtual bool on_writable() = 0;

    // Called when the reactor finds this resource has hanged up.
    //
    // This method is called when the reactor detects unexpected hangup.
    virtual bool on_hangup() {
        return invalidate();
    }

    // Called when the reactor finds an error on this resource.
    //
    // This method is called when the reactor detects some error.
    virtual bool on_error() {
        return invalidate();
    }

protected:
    const int m_fd;
    reactor* m_reactor = nullptr;

    friend class reactor;

private:
    bool m_valid = true;
    mutable spinlock m_lock;
    typedef std::unique_lock<spinlock> lock_guard;

    void invalidate_(lock_guard g) {
        m_valid = false;
        g.unlock();
        on_invalidate();
    }
};


// The reactor.
//
// This reactor internally uses epoll with edge-triggered mode.
class reactor {
public:
    reactor();
    ~reactor();

    // Events to poll.
    enum reactor_event {
        EVENT_IN = EPOLLIN,
        EVENT_OUT = EPOLLOUT,
    };

    // Add a resource to the reactor.
    // @events `EVENT_IN`, `EVENT_OUT`, or bitwise OR of them.
    //
    // Add a resource to the reactor.  The ownership of the resource
    // will be moved to the reactor.  Only the reactor thread can use this.
    void add_resource(std::unique_ptr<resource> res, int events);

    // Modify epoll events for a resource.
    // @events `EVENT_IN`, `EVENT_OUT`, or bitwise OR of them.
    void modify_events(const resource& res, int events);

    // Run the reactor loop.
    // @callback  Function called at each interval.
    // @interval  Interval between two callbacks in seconds.
    void run(std::function<void(reactor& r)> callback, int interval = 1);

    // Run the reactor once.
    void run_once() { poll(); }

    // Return the number of registered and still valid resources.
    std::size_t size() const { return m_resources.size(); }

    // Quit the run loop gracefully.
    //
    // Call this to quit the run loop gracefully.
    // Only the reactor thread can use this.
    void quit() { m_running = false; }

    // Invalidate all registered resources.
    //
    // Invalidate all registered resources to unblock other threads.
    void invalidate() {
        for( auto& t: m_resources )
            t.second->invalidate();
    }

    // Add a resource to the readable resource list.
    //
    // Only <resource::on_readable> may call this when it stops reading
    // from a resource before it encounters `EAGAIN` or `EWOULDBLOCK`.
    void add_readable(const resource& res) {
        m_readables.push_back(res.fileno());
    }

    // Add a removal request for a resource.
    //
    // Resources can be shared with threads other than the reactor thread.
    // When such a thread successfully invalidates a resource, the thread
    // need to request the reactor thread to remove the resource by
    // calling this.
    void request_removal(const resource& res) {
        lock_guard g(m_lock);
        m_drop_req.push_back(res.fileno());
    }

    bool has_garbage() const noexcept {
        return ! m_garbage.empty();
    }

    // Fix the garbage resources to be destructed at the next <gc>.
    //
    // Fix the garbage resources to be destructed at the next <gc>.
    // Only the reactor thread can use this.
    //
    // @return `false` if there are no garbage resources, `true` otherwise.
    bool fix_garbage() {
        if( ! m_garbage_copy.empty() )
            throw std::logic_error("No gc() was called after fix_garbage()!");
        std::size_t n = m_garbage.size();
        if( n == 0 ) return false;

        logger::debug() << "reactor: collecting " << n << " resources.";
        m_garbage_copy.swap(m_garbage);
        return true;
    }

    // Destruct garbage resources fixed by <fix_garbage>.
    //
    // Destruct garbage resources fixed by <fix_garbage>.
    // Only the reactor thread can use this.
    void gc() {
        logger::debug() << "reactor: collected garbage resources.";
        m_garbage_copy.clear();
    }

    // Remove a registered resource.
    // This is only for the reactor thread.
    void remove_resource(const resource& res) {
        remove_resource(res.fileno());
    }

private:
    const int m_fd;
    bool m_running;
    typedef std::unordered_map<int, std::unique_ptr<resource>> resource_map;
    resource_map m_resources;
    std::vector<int> m_readables;
    std::vector<int> m_readables_copy;

    // resource close request queue and its guarding lock.
    mutable spinlock m_lock;
    typedef std::lock_guard<spinlock> lock_guard;
    std::vector<int> m_drop_req;
    std::vector<int> m_drop_req_copy;

    // pending destruction lists
    std::vector<std::unique_ptr<resource>> m_garbage;
    std::vector<std::unique_ptr<resource>> m_garbage_copy;

    void remove_resource(int fd);
    void poll();
};


inline void resource::invalidate_and_close() {
    lock_guard g(m_lock);
    if( ! m_valid ) return;
    invalidate_( std::move(g) );
    m_reactor->request_removal(*this);
}

} // namespace cybozu

#endif // CYBOZU_REACTOR_HPP
