#<cldoc:Differences from memcached>

Differences from memcached.

Differences from memcached 1.4.15
---------------------------------

* `stats` returns different items.  
  `stats slabs` is not implemented.
* `slabs automove` and `slabs reassign` are not implemented.  
  These always return "OK".
* `verbosity` takes a string argument rather than an integer.  
  Valid values are `error`, `warning`, `info`, and `debug`.
* Implementations of binary protocol command `GaT` and `Get` are identical.  
  Both can have optional expiration time.  Objects will be touched
  only when the command is attended with an expiration time.
* UDP transport is not implemented.
* UNIX domain transport is not, and will not be, implemented.
* SASL authentication is not implemented.  Who cares?
