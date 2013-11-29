#<cldoc:Benchmark Results>

Benchmark results.

Environment
-----------

* CPU  
    Intel Xeon E5507 2.27 GHz * 2 (total 8 physical cores)
* OS  
    Ubuntu 12.04
* Threads  
    8 worker threads for yrmcds / memcached
* Memory  
    1 GiB for yrmcds / memcached
* Buckets  
    5 million for yrmcds

Benchmark Tool
--------------

[memslap][] from [libmemcached][].

Parameters: `--threads=8 --concurrency=80`

Other parameters are left default, i.e., memslap runs for 600 seconds with set/get ratio = 1:9.

memslap ran on a different machine.

Versions
--------

* memcached 1.4.13
* yrmcds 0.9.7

Results
-------

* memcached  
    Ops: 71584331 TPS: 119298 Net_rate: 126.6M/s
* yrmcds (0 slave)  
    Ops: 81028171 TPS: 135037 Net_rate: 119.2M/s
* yrmcds (1 slave)  
    Ops: 69657147 TPS: 116086 Net_rate: 113.4M/s
* yrmcds (2 slaves)  
    Ops: 63479541 TPS: 105791 Net_rate: 101.2M/s


[memslap]: http://docs.libmemcached.org/bin/memslap.html
[libmemcached]: http://libmemcached.org/libMemcached.html
