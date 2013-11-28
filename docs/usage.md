#<cldoc:Usage>

Configuration and execution of `yrmcdsd`.

`yrmcdsd` is the server program of yrmcds.  Its built-in configurations are
reasonably good to run as a stand-alone server.  For replication, you need
to specify a virtual IP address and configure a clustering software such as
[keepalived][].

Files
-----

Usually, yrmcds is installed under `/usr/local`.

* /usr/local/etc/yrmcds.conf  
    The default configuration file.
* /usr/local/sbin/yrmcdsd  
    The program.

Configuration
-------------

You can change any of these configuration options through the
configuration file:

* `user` (Default: none)  
    If set, the program will try to `setuid` to the given user.
* `group` (Default: none)  
    If set, the program will try to `setgid` to the given group.
* `virtual_ip` (Default: 127.0.0.1)  
    The master's virtual IP address.
    Both IPv4 and IPv6 addresses are supported.
* `port` (Default: 11211)  
    memcache protocol port number.
* `repl_port` (Default: 11213)  
    The replication protocol port number.
* `max_connections` (Default: 0)  
    Maximum number of client connections.  0 means unlimited.
* `temp_dir` (Default: /var/tmp)  
    Directory to store temporary files for large objects.
* `log.threshold` (Default: info)  
    Log threshold.  Possible values: `error`, `warning`, `info`, `debug`.
* `log.file` (Default: standard error)  
    Logs will be written to this file.
* `buckets` (Default: 1000000)  
    Hash table size.
* `max_data_size` (Default: 1M)  
    The maximum object size.
* `heap_data_limit` (Default: 256K)  
    Objects larger than this will be stored in temporary files.
* `memory_limit` (Default: 1024M)  
    The amount of memory allowed for yrmcdsd.
* `workers` (Default: 8)  
    The number of worker threads.
* `gc_interval` (Default: 10)  
    The interval between garbage collections in seconds.

Running yrmcdsd
---------------

Just run `yrmcdsd`.

The program does not run as traditional UNIX daemons.
Instead, use [upstart][] or [systemd][] to run `yrmcdsd` as a background service.

Do not forget to increase the maximum file descriptors.
A sample upstart script is available at [etc/upstart](https://github.com/cybozu/yrmcds/blob/master/etc/upstart).

Replication
-----------

The replication of yrmcds servers is based on a virtual IP address.
To assign a virtual IP address to a live server, a clustering software
such as [keepalived][] or [Pacemaker][pacemaker] is used.

Our recommendation is to use [keepalived][].

### keepalived

For stability, keepalived should be configured in non-preemptive mode.

A sample configuration file is available at [etc/keepalived.conf](https://github.com/cybozu/yrmcds/blob/master/etc/keepalived.conf).

### How replication works

`yrmcdsd` **periodically watches** if the server owns the virtual IP address
or not.  If `yrmcdsd` finds it holds the virtual IP address, that server
will promote itself as the new master.  All others become slaves.

Slaves connect to the master through the virtual IP address.  If the
connection resets, `yrmcdsd` waits some seconds to see if the virtual IP
address is assigned.  If assigned, the slave becomes the new master.  If
not, the slave drops all objects then try to connect to the new master.

Since the master node is elected dynamically by [keepalived][], each
`yrmcdsd` should have the same configuration parameters.


[keepalived]: http://www.keepalived.org/
[pacemaker]: http://clusterlabs.org/wiki/Main_Page
[upstart]: http://upstart.ubuntu.com/
[systemd]: http://www.freedesktop.org/wiki/Software/systemd/
