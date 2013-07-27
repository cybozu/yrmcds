yrmcds
======

yrmcds is a memory object caching system with master/slave replication.
Since its protocol is perfectly compatible with that of [memcached][],
yrmcds can be used as a drop-in replacement for [memcached][].

The biggest benefit of yrmcds is its amazingly low cost replication system.
The master server is elected dynamically from a group of servers, which
eliminates static master/slave configurations.  By adopting virtual-IP
based replication, no modifications to applications are required.

Unlike [repcached][], yrmcds is not a patch for memcached.  No piece of
code are shared between yrmcds and memcached.  yrmcds is developed for
[cybozu.com][cybozu], a B2B cloud service widely adopted by companies in
Japan.

Features
--------

* Memcached text and binary protocols.  
    Almost all commands are implemented including undocumented `Touch`,
    `GaT`, and `GaTK` binary commands.
* [Server-side locking](docs/locking.md).
* Large objects can be stored in temporary files, not in memory.
* Virtual-IP based master-slave replication.
* Automatic fail-over.
* Automatic recovery of redundancy.

See also [usage guide](docs/usage.md), [future plans](docs/future.md),
[differences from memcached](docs/diffs.md), [design notes](docs/design.md)
and some [benchmark results](docs/bench.md).

Prerequisites
-------------

* Fairly recent Linux kernel.
* C++11 compiler (gcc 4.8.1+ or clang 3.3+).
* [TCMalloc][tcmalloc] from Google.
* GNU make.

The following may help Ubuntu users to compile gcc 4.8.1:
```shell
sudo apt-get install libgmp-dev libmpfr-dev libmpc-dev build-essential
tar xjf gcc-4.8.1.tar.bz2
mkdir gcc-4.8.1/build
cd gcc-4.8.1/build
../configure --prefix=/usr/local/gcc --disable-shared --disable-multilib \
             --enable-threads --enable-__cxa_atexit --enable-languages=c,c++ \
             --disable-nls
make -j 4 BOOT_CFLAGS=-O2 bootstrap
sudo make install
make clean
export PATH=/usr/local/gcc/bin:$PATH
```

Build
-----

1. Prepare TCMalloc.  
  On Ubuntu, run `apt-get install libgoogle-perftools-dev`.
2. Run `make`.

You can build yrmcds without TCMalloc by editing Makefile.

Install
-------

On Ubuntu, `sudo make install` installs yrmcds under `/usr/local`.  
An upstart script and a logrotate configuration file are installed too.

[memcached]: http://memcached.org/
[repcached]: http://repcached.lab.klab.org/
[cybozu]: https://www.cybozu.com/us/
[tcmalloc]: http://goog-perftools.sourceforge.net/doc/tcmalloc.html
