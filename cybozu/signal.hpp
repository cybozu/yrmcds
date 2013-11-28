/* Signal handling resource using signalfd(2).
 *
 * (C) 2013 Cybozu.
 */

#ifndef CYBOZU_SIGNAL_HPP
#define CYBOZU_SIGNAL_HPP

#include "reactor.hpp"
#include "util.hpp"

#include <functional>
#include <initializer_list>
#include <memory>
#include <sys/signalfd.h>

namespace cybozu {

// A <cybozu::resource> subclass for [`signalfd`](http://manpages.ubuntu.com/manpages/precise/en/man2/signalfd.2.html).
class signal_reader: public resource {
public:
    using callback_t = std::function<void(const struct signalfd_siginfo&,
                                          reactor&)>;

    // Constructor.
    // @mask      A set of signals to be handled by this resource.
    // @callback  Callback function to handle received signals.
    //
    // Construct a signal reading resource.  Signals set in `mask`
    // need to be blocked on all threads.
    signal_reader(const sigset_t *mask, callback_t callback):
        resource( signalfd(-1, mask, SFD_NONBLOCK|SFD_CLOEXEC) ),
        m_callback(callback) {
        if( m_fd == -1 )
            throw_unix_error(errno, "signalfd");
    }
    // Constructor.
    // @mask      A set of signals to be handled by this resource.
    //
    // Construct a signal reading resource.  Signals set in `mask`
    // need to be blocked on all threads.  This constructor leaves
    // the callback function empty.  Use `set_handler` to set one.
    explicit signal_reader(const sigset_t *mask):
        signal_reader(mask, nullptr) {}

    virtual ~signal_reader() {}

    void set_handler(const callback_t& callback) {
        m_callback = callback;
    }

private:
    callback_t m_callback;

    virtual bool on_readable() override final {
        while( true ) {
            struct signalfd_siginfo si;
            ssize_t n = read(m_fd, &si, sizeof(si));
            if( n == -1 ) {
                if( errno == EINTR ) continue;
                if( errno == EAGAIN || errno ==EWOULDBLOCK ) return true;
                throw_unix_error(errno, "read");
            }
            if( n != sizeof(si) )
                throw std::runtime_error("<signal_reader::on_readable> bug?");
            if( m_callback ) m_callback(si, *m_reactor);
        }
    }
    virtual bool on_writable() override final { return true; }
};


// Block signals and create <signal_reader> for blocked signals.
// @sigs  List of signals to be blocked.
//
// This function blocks given signals and creates a <signal_reader>
// prepared to read blocked signals.  In addition, this configures
// `SIGPIPE` to be ignored, and `SIGABRT` produces the stack trace.
//
// This function should be called from the main thread before any
// other threads are created.
std::unique_ptr<signal_reader> signal_setup(std::initializer_list<int> sigs);


} // namespace cybozu

#endif // CYBOZU_SIGNAL_HPP
