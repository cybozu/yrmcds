#<cldoc:Future Plans>

List of future plans.

Future Plans
------------

* Replicate CAS unique values.  
    Currently, yrmcds does not replicate CAS values, which may lead to
    **false-positives** as well as (harmless) false-negatives.  
    <p />

* Allow eviction to be disabled and honor the memory limit.  
    This is a requirement for in-memory database.  
    <p />

* Save/load cached objects at program exit/startup.  
    This is another requirement for in-memory database.  
    <p />

* Save snapshots periodically.  
    This can be done by [forking][fork] a child process when all
    worker threads are idle.


[fork]: http://manpages.ubuntu.com/manpages/precise/en/man2/fork.2.html
