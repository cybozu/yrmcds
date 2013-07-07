// CRTP thread wrapper.
// (C) 2013 Cybozu.

#ifndef CYBOZU_THREAD_HPP
#define CYBOZU_THREAD_HPP

#include "logger.hpp"
#include "util.hpp"

#include <stdexcept>
#include <stdlib.h>
#include <system_error>
#include <thread>

namespace cybozu {

// A CRTP template wrapping <std::thread>.
//
// A curiously-recurring-template-pattern (CRTP) template to wrap
// <std::thread> with associated data structures.  Derived classes
// need to implement `void run()`.
//
// <std::system_error> and <std::exception> will be caught and logged.
// If `exit_on_exception` is not 0, then `exit(exit_on_exception)` is
// called to terminate the program.
template<typename Derived, int exit_on_exception=1>
class thread_base {
public:
    void start() {
        if( m_thread.joinable() )
            throw std::logic_error("Thread already started!");
        m_done.store(false, std::memory_order_relaxed);
        m_thread = std::thread([this]() { run_in_try(); });
    }

    void run_in_try() {
        try {
            static_cast<Derived*>(this)->run();
            m_done.store(true, std::memory_order_release);

        } catch( const std::system_error& e ) {
            demangler t(typeid(e).name());
            logger::error() << "[" << t.name() << "] "
                            << "(" << e.code() << ") "
                            << e.what();
            if( exit_on_exception != 0 )
                ::exit(exit_on_exception);

        } catch( const std::exception& e ) {
            demangler t(typeid(e).name());
            logger::error() << "[" << t.name() << "] "
                            << e.what();
            if( exit_on_exception != 0 )
                ::exit(exit_on_exception);
        }
    }

    bool done() const noexcept {
        return m_done.load(std::memory_order_acquire);
    }

protected:
    std::atomic<bool> m_done;
    std::thread m_thread;
};

} // namespace cybozu

#endif // CYBOZU_THREAD_HPP
