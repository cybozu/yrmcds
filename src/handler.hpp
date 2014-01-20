// Protocol-sepcific logics and data structures
// (C) 2014 Cybozu.

#ifndef YRMCDS_HANDLER_HPP
#define YRMCDS_HANDLER_HPP

#include <cybozu/reactor.hpp>

namespace yrmcds {

class protocol_handler {
public:
    virtual ~protocol_handler();

    // Called when the server starts.
    virtual void on_start();

    // Called when the server enters the master mode.
    virtual void on_master_start();

    // Called right before the <check> of <syncer>.
    virtual void on_master_pre_sync();

    // Called when the intervals of the reactor loop.
    virtual void on_master_interval();

    // Called when the server leaves the master mode.
    virtual void on_master_end();

    // Called when the server enters the slave mode.
    //
    // @param fd  a socket to the master.
    virtual void on_slave_start(int fd);

    // Called when the intervals of the reactor loop.
    virtual void on_slave_interval();

    // Called when the server leaves the slave mode.
    virtual void on_slave_end();

    // Called when the server discards all stored data.
    virtual void on_clear();

    // If this protocol handler ready to run the reactor GC,
    // returns true. Otherwise, return false.
    virtual bool reactor_gc_ready();
};

} // namespace yrmcds

#endif // YRMCDS_HANDLER_HPP
