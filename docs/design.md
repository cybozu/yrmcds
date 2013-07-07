#<cldoc:Design Notes>

Design Notes
============

Automatic master promotion
--------------------------

The standard set of yrmcds consists of a master server and two slave
servers.  A set of yrmcds will be prepared for each server farm.

To elect the master server automatically, keepalived or similar
virtual IP address allocation tool is used.  The server owning the
virtual IP address becomes the master server.  The others are slaves.

Slaves connect to the master via the virtual IP address.  The master
will *not* connect to slaves.  A new slave will receive all data from
the master server once the connection has been established.

For some reason, if the master suddenly notices it is no longer
the master (i.e. the server has lost the virtual IP), it kills itself
to reborn as a new slave (restarted by upstart).

If a slave gets disconnected from the master, it waits for some
seconds to see if the virtual IP address is assigned.  If assigned,
the slave promotes to the new master.  If not, the slave forgets all
replicated data then try to connect to the new master.  The new master
should close the established connection to the old master, if any.

Replication protocol
--------------------

The replication protocol is the same as [the binary protocol of memcached][1].
Specifically, slaves receive only "SetQ" and "DeleteQ" requests.

This allows any memcached compatible programs can become yrmcds slaves
with slight modifications.

The number of connections
-------------------------

The master will have tens of thousands of client connections from
every PHP-fpm processes in a server farm.  A thread per connection
model is therefore not very efficient.

This encourages us to adopt asynchronous sockets and [the reactor pattern][2].

The reactor and workers
-----------------------

So we adopt [the reactor pattern with a thread-pool][3] for the best
performance.

The reactor is implemented with asynchronous sockets and [epoll][] in
edge-triggered mode.  The reactor thread receives data up to a certain
limit for a thread to keep fairness between clients.

Basically, with edge-triggered mode, a socket need to be read until
[recv][] returns an `EWOULDBLOCK` error.  If the reactor stops receiving
data before `EWOULDBLOCK`, the reactor need to remember that socket and
receive data from the socket separately from epoll.
To remember such sockets, a list of _readable_ socket is used.

Once some data received for a socket, the socket is dispatched to a worker
thread.  The reactor will *not* receive data from the socket while the
worker is processing data.  This is necessary to keep the order of
received data.  Again, the list of readable socket is used to remember
such sockets.  To indicate a socket being in use, each socket has an
atomic flag that is set by the reactor thread and will be cleared by
a worker thread.

Every worker thread has a fairly large buffer to receive data, an atomic
flag to indicate whether the worker is busy or idle, and an [eventfd][]
file descriptor.

When the reactor finds some sockets are readable, it first tries to find
an idle worker.  If there is no idle worker, the reactor gives up receiving
by adding the readable sockets to the readable socket list.
This is because the reactor thread must not be blocked; otherwise, deadlocks
may happen when all the worker threads are blocking to send large data.

If the reactor finds an idle worker, it receives data from a socket into
the buffer of the idle worker, set the busy flag of the worker and the
socket, then finally notify the worker thread of new data by writing to
the eventfd descriptor of the worker.

Once the worker finished to consume the data, the worker issues a full
memory barrier, clears the socket's busy flag, clears the busy flag of
the worker itself, then read from the eventfd descriptor to wait for
the next job.

BTW, why does the reactor rather than a worker receive data?
There are two reasons.  One is to eliminate a lock to protect a list of
readable sockets.  Another is that it is difficult to keep the order
of received data between worker threads.

And why is eventfd rather than condition variables used?  This is because,
unlike condition variables, eventfd remembers events so that workers
never fail to catch events.

Efficient allocation of readable lists
--------------------------------------

Readable lists used in the reactor can be implemented with very rare
memory allocations by preparing two lists that pre-allocate a certain
amount of memory.  Swap the list with another for each epoll loop.

The hash
--------

The biggest and the most important data structure in yrmcds is clearly
the hash map of objects.  To reduce contention between worker threads,
the hash does not use a single lock to protect the data.  Instead, each
bucket has a lock to protect objects in the bucket and the bucket itself.

One drawback is that resizing the hash is almost impossible.  In the real
implementation, yrmcds statically allocates a large number of buckets.

Housekeeping
------------

yrmcds is essentially an object _cache_ system.  Objects whose life-time
have been expired need to be removed from the cache.  If the cache is
full of objects, least-recently-used (LRU) objects should be removed.

For such housekeeping, a dedicated thread called **GC thread** is used.
It scans the whole hash to detect and remove expired objects.

To implement LRU cache, objects have a counter that increments at every
GC.  The counter is reset to zero when a worker accesses the object.
When the cache is full, GC thread removes objects whose LRU counter value
is high.

A GC thread is started by the reactor thread only when there is no
running GC thread.  The reactor thread passes the current list of slaves
to the GC thread in order to replicate object removals.  If a slave
is added while a GC thread is running, that slave may fail to remove
some objects, which is *not* a big problem.

Replication
-----------

When a slave connect to the master, it need to receive all data in the
master.  yrmcds reuses the GC thread for this initial replication because
the GC thread scans the entire hash.

While GC is running, the reactor and worker threads also keep running.
The GC thread and worker threads therefore need to be synchronized when
sending data of the same object.  This can be achieved by keeping the
lock of a hash bucket acquired while an object is being sent.

Sockets for replication
-----------------------

Sockets connected to slaves are used to send a lot of replication data.
Further, worker threads serving client requests need to access replication
sockets to send updates to slaves.

Another difference from client sockets is the possible number of sockets.
The number of replication sockets can be limited to a fairly small value,
say 5.

Worker threads need to have references to all replication sockets.
This can be done without any locks; the reactor passes a copy of the
references along with the socket and received data.  However, this can
introduce a race between workers and the GC thread.

If a new GC thread starts the initial replication process for a new slave
while some worker threads having old copies of replication sockets are
executing its job, the workers would fail to send some objects to the new
slave.

To resolve this race, we need to guarantee that all worker threads
are idle or have the latest copies of replication sockets.  This can be
implemented as:

1. Accept a new replication socket and add it to the list of
   replication sockets.  
   At this point, the GC thread must not begin the initial replication
   for this new slave.
2. The reactor thread puts a synchronization request.  
   The request will be satisfied once the reactor thread observes every
   worker thread gets idle.
3. The reactor thread adds the new replication socket to the confirmed
   list.
4. At the next GC, the reactor thread requests initial replication for
   sockets stored in the confirmed list.

Sending data
------------

As yrmcds need to handle a lot of clients, having every socket has a
large send buffer is not a good idea.  Sockets connected to clients
therefore can have temporarily allocated memory for pending data only.

On the other hand, sockets connected to slaves are few, hence they can
statically allocate large memory for pending send data.

Anyways, a sending thread need to wait for the reactor to send pending
data and clear the buffer when there is not enough room.

To achieve best TCP performance, we use [`TCP_CORK`][4] option although
it is Linux-specific.

With `TCP_CORK` turned on, the TCP send buffer need to be flushed explicitly.
The buffer should be flushed only when all data are successfully sent without
seeing `EWOULDBLOCK`.

To put a complete response data with one call, the socket object should
provide an API similar to [writev][].

Reclamation strategy of shared sockets
--------------------------------------

### Sockets are shared

Sockets connected to clients may be shared by the reactor thread and
a worker thread.  Sockets connected to slaves may be shared by the
reactor thread, worker threads, and the initial replication thread.

### Socket can be closed only by the reactor thread

This is because the reactor thread manages a mapping between file
descriptors and resource objects including sockets.  If another thread
closes a file descriptor of a resource, the same file descriptor number
may be reused by the operating system, which would break the mapping.

### Strategy to destruct shared sockets

1. Sockets can be invalidated by any thread.  
    Invalidated sockets refuse further access to them.
2. Inform the reactor thread of invalidated sockets.
3. The reactor thread  
    1. removes invalidated sockets from the internal mapping,
    2. closes the file descriptor, then
    3. adds them a list of pending destruction resources.

At some point when there is no running GC thread, the reactor thread can
put a new synchronization request.  To optimize memory allocations,
the reactor thread will not put such a request if there are any pending
requests.  This way, the reactor can save the current pending destruction
list by swapping contents with a pre-allocated save list.

Once the reactor observes idle state of all worker threads, resources
in the save list can be destructed safely.

No blocking job queues, no barrier synchronization
--------------------------------------------------

To avoid excessive contention between the reactor and worker threads,
a blocking job queue is not used between them.  Instead, the reactor
thread loops over atomic flags for each worker thread to identify if
a worker is idle or busy.

For the same reason, we do not use barrier synchronization between
the reactor and worker threads.  Instead, the reactor thread checks
every worker thread is idle or becomes idle when there are any
synchronization request.  Once it confirms all worker threads gets
idle at least once after a synchronization request, it starts a job
associated with the synchronization request.

Slaves are essentially single-threaded
--------------------------------------

Slaves do accept connection requests from memcache clients, but they
close the connections immediately.  This way, slaves can essentially
be single-threaded.  As long as being a slave, the internal hash need
not be guarded by locks.  For this reason, the internal hash should
provide lock-free version of methods.

Joining threads
---------------

All threads are started by the reactor thread.  The reactor thread is
therefore responsible for those threads to be joined.

All threads except for the reactor thread may block on either a
condition variable of a socket or the [eventfd][] file descriptor.
For sockets, the reactor thread signals the condition variable to unblock
threads when it closes the socket.  For eventfd descriptors, the reactor
thread simply writes to it.

When the program exits, the reactor thread executes the following
termination process:

1. Sets termination flags of all worker threads.
2. Writes to eventfd descriptors to unblock worker threads.
3. invalidates all resources to unblock worker threads.
4. joins with the GC thread, if any, then
5. destructs resources.

Large data
----------

Too large data should be stored in a temporary file.
The temporary file shall soon be unlinked for automatic removal
upon the program exit.

Summary
-------

### Threads

In the master,
* the reactor thread,
* worker threads to process client requests, and
* a GC thread.

Slaves run the reactor (main) thread only.

### Sockets

In the master,
* a listening socket for clients,
* a listening socket for slaves,
* sockets connected to clients, and
* sockets connected to slaves.

In a slave,
* a listening socket for clients, and
* a socket connected to the master.

### Shared data structures

* Connected sockets.
* The object hash map.
* Close request queue in the reactor.
* Other less significant resources such as statistics counters.

### Locks and ordering

* The reactor thread
    * acquires a lock of a socket to send data.
    * acquires a spinlock of socket close request queue.
* A worker thread
    * acquires a lock of a hash bucket.
        * acquires a lock of a socket.
    * acquires a lock of a socket to send data independent of cached objects.
    * acquires a spinlock of the reactor to put socket close request.
* A GC thread
    * acquires a lock of a hash bucket.
        * acquires locks of sockets connected to slaves.
    * acquires a spinlock of the reactor to put socket close request.

[1]: https://code.google.com/p/memcached/wiki/BinaryProtocolRevamped
[2]: http://en.wikipedia.org/wiki/Reactor_pattern
[3]: http://stackoverflow.com/questions/14317992/thread-per-connection-vs-reactor-pattern-with-a-thread-pool
[4]: http://manpages.ubuntu.com/manpages/precise/en/man7/tcp.7.html
[epoll]: http://manpages.ubuntu.com/manpages/precise/en/man7/epoll.7.html
[eventfd]: http://manpages.ubuntu.com/manpages/precise/en/man2/eventfd.2.html
[recv]: http://manpages.ubuntu.com/manpages/precise/en/man2/recv.2.html
[signalfd]: http://manpages.ubuntu.com/manpages/precise/en/man2/signalfd.2.html
[writev]: http://manpages.ubuntu.com/manpages/precise/en/man2/writev.2.html
