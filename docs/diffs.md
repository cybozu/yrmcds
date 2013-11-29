#<cldoc:Differences from memcached>

Differences from memcached.

Differences from memcached 1.4.15
---------------------------------

* Implementations of binary protocol command `GaT` and `Get` are identical.  
  Both may have an optional expiration time.  Objects will be touched
  only when the command is attended with an expiration time.
* `stats` returns different items.  
  `stats slabs` is not implemented.
  `stats ops` returns ops counts for each text/binary command.
* `slabs automove` and `slabs reassign` are not implemented.  
  These always return "OK".
* `verbosity` takes a string argument rather than an integer.  
  Valid values are `error`, `warning`, `info`, and `debug`.
* UDP transport is not implemented.
* UNIX domain transport is not, and will not be, implemented.
* SASL authentication is not implemented.  Who cares?
