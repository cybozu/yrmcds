// Protocol-sepcific logics and data structures.
// (C) 2014 Cybozu.

#ifndef YRMCDS_HANDLER_HPP
#define YRMCDS_HANDLER_HPP

#include <cybozu/logger.hpp>

namespace yrmcds {

// An interface for protocol-specific logics.
class protocol_handler {
public:
    virtual ~protocol_handler() {}

    // Called when the server starts.
    virtual void on_start() {}

    // Called when the server exits.
    virtual void on_exit() {}

    // Called when the server enters the master mode.
    virtual void on_master_start() {}

    // Called when the intervals of the reactor loop.
    virtual void on_master_interval() {}

    // Called when the server leaves the master mode.
    virtual void on_master_end() {}

    // Called when the server enters the slave mode.
    //
    // If this function succeeded, returns `true`.
    // Otherwise, returns `false`.
    virtual bool on_slave_start() { return true; }

    // Called when the intervals of the reactor loop.
    virtual void on_slave_interval() {}

    // Called when the server leaves the slave mode.
    virtual void on_slave_end() {}

    // Called to dump the statistics.
    //
    // Implementations should use `cybozu::logger::info()` to emit stats.
    virtual void dump_stats() = 0;

    // Called when the server discards all stored data.
    virtual void clear() {}

    // If this protocol handler is ready for the reactor GC,
    // returns true. Otherwise, return false.
    virtual bool reactor_gc_ready() const { return true; }
};

} // namespace yrmcds

#endif // YRMCDS_HANDLER_HPP
