#<cldoc:Future Plans>

List of future plans.

Future Plans
------------

* Randomize hash seed to prevent some kind of attacks.  
    Or should [MurmurHash][murmur] be replaced with [SipHash][siphash]?  
    <p />

* Replicate CAS unique values.  
    Currently, yrmcds does not replicate CAS values, which may lead to
    **false-positives** as well as (harmless) false-negatives.  
    <p />

* Improve <cybozu::hash_map>.  
    Possible optimizations are:
    - Use a simple linked-list in a bucket instead of <std::vector>.
    - Require move-ability of objects to eliminate <std::unique_ptr>.
    - Pass function objects by reference.  
    <p />

* Allow eviction to be disabled and honor the memory limit.  
    This is a requirement for in-memory database.  
    <p />

* Save/load cached objects at program exit/startup.  
    This is another requirement for in-memory database.  
    <p />

* Save snapshots periodically.  
    This can be done by [forking][fork] a child process at slaves.
    Since slaves are single-threaded, fork will do no harm.


[murmur]: https://code.google.com/p/smhasher/wiki/MurmurHash3
[siphash]: https://131002.net/siphash/
[fork]: http://manpages.ubuntu.com/manpages/precise/en/man2/fork.2.html
[keepalived]: http://www.keepalived.org/
