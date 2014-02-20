#<cldoc:Semaphore support>

Semaphore support

Overview
--------

A *semaphore* is a variable used for controlling accesses by multiple processes.

As an extension, yrmcds provides a distributed semaphore functions.

Each semaphore has a non-empty name.  The maximum name is up to 65535 bytes.  The number of resources available for a semaphore is represented as a 4 byte unsigned integer; this means a semaphore can have up to 4294967295 resources.

- The semaphore extension is disabled by default.  It can be enabled by a configuration file.
- TCP port used for the semaphore protocol is different from that of the memcache protocol.
- The namespace of semaphore objects is different from that of the memcache objects.
- When a connection is closed, all semaphore objects acquired by the connection 
are released automatically.
- A semaphore is created dynamically at the first time the resources managed by the semaphore are acquired.
- Semaphores will be deleted by the GC thread if all resources managed by them are released.
- Semaphores are not replicated.


Protocol
--------

The semaphore extension has its own binary protocol.

### General format of a packet

```
 Byte/     0       |       1       |       2       |       3       |
    /              |               |               |               |
   |0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|
   +---------------+---------------+---------------+---------------+
  0/ HEADER                                                        /
   /                                                               /
   /                                                               /
   /                                                               /
   +---------------+---------------+---------------+---------------+
 12/ COMMAND-SPECIFIC BODY (as needed)                             /
   +---------------+---------------+---------------+---------------+
```


### Request header

```
 Byte/     0       |       1       |       2       |       3       |
    /              |               |               |               |
   |0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|
   +---------------+---------------+---------------+---------------+
  0| Magic         | Opcode        | Flags         | Reserved      |
   +---------------+---------------+---------------+---------------+
  4| Body length                                                   |
   +---------------+---------------+---------------+---------------+
  8| Opaque                                                        |
   +---------------+---------------+---------------+---------------+
   Total 12 bytes
```

- `Magic` is `0x90`.
- `Opcode` is defined later.
- `Flags` is defined later.
- `Reserved` is always zero.
- `Body length` is a 4 byte unsigned integer in big-endian.
- `Opaque` is an arbitrary 4 byte data.


### Response header

```
 Byte/     0       |       1       |       2       |       3       |
    /              |               |               |               |
   |0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|
   +---------------+---------------+---------------+---------------+
  0| Magic         | Opcode        | Status        | Reserved      |
   +---------------+---------------+---------------+---------------+
  4| Body length                                                   |
   +---------------+---------------+---------------+---------------+
  8| Opaque                                                        |
   +---------------+---------------+---------------+---------------+
   Total 12 bytes
```

- `Magic` is `0x91`.
- `Opcode` and `Opaque` are copied from the corresponding request.
- `Status` is defined later.
- `Reserved` is always zero.
- `Body length` is a 4 byte unsigned integer in big-endian.


### Opcodes

| Code   | Operation
|--------|-----------
| `0x00` | `Noop`
| `0x01` | `Get`
| `0x02` | `Acquire`
| `0x03` | `Release`
| `0x10` | `Stats`


### Flags

No flags are yet defined.


### Response status

| Code   | Status
|--------|-------------------
| `0x00` | No error
| `0x01` | Not found
| `0x04` | Invalid arguments
| `0x21` | Resource not available
| `0x22` | Not acquired
| `0x81` | Unknown command
| `0x82` | Out of memory


Packet specifications
---------------------

Unless otherwise noted, all integers are represented in big-endian.


### Error responses

Errors are signaled by the `Status` field of the response header.
Error messages are accompanied in the message body.


### Noop

`Noop` does nothing and returns a success response.

The request and response of `Noop` has no body.


### Acquire

`Acquire` tries to acquire resources of a semaphore:

Request body:

```
 Byte/     0       |       1       |       2       |       3       |
    /              |               |               |               |
   |0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|
   +---------------+---------------+---------------+---------------+
  0| Resources                                                     |
   +---------------+---------------+---------------+---------------+
  4| Initial resources                                             |
   +---------------+---------------+---------------+---------------+
  8| Name length                   | (Name data) ...
   +---------------+---------------+
   Total 10 + (Name length) bytes
```

Successful response body:

```
 Byte/     0       |       1       |       2       |       3       |
    /              |               |               |               |
   |0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|
   +---------------+---------------+---------------+---------------+
  0| Resources                                                     |
   +---------------+---------------+---------------+---------------+
   Total 4 bytes
```

- `Resources` is a 4-byte unsigned integer number.
  This request will acquire this count of resources.
  If this is zero, `Invalid arguments` is returned.
- `Initial` is a 4-byte unsigned integer number.
  This is used to initialize the value of a new semaphore.
  `Initial` must be equal to or greater than `Resources`, otherwise
  `Invalid arguments` is returned.
- `Name length` is a 2-byte unsigned integer number.
  If this is zero, `Invalid arguments` is returned.

If the named semaphore does not exist, `Acquire` creates a new semaphore whose value is `Initial - Resources`, and returns the success.

Otherwise, if `Resources` is equal to or less than the value of the semaphore, this request will minus `Resources` from the semaphore value then returns the success.  Else, this returns `Resource not available` error.


### Release

`Release` returns resources back to the semaphore.

Request body:

```
 Byte/     0       |       1       |       2       |       3       |
    /              |               |               |               |
   |0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|
   +---------------+---------------+---------------+---------------+
  0| Resources                                                     |
   +---------------+---------------+---------------+---------------+
  4| Name length                   |  (Name data) ...
   +---------------+---------------+
   Total 6 + (Name length) bytes
```

A successful response has no body.

- `Resources` is the count of resources to return.
  It can be zero.
- `Name length` is a 2-byte unsigned integer number.
  If this is zero, `Invalid arguments` is returned.

If the named semaphore does not exist, `Release` returns `Not found` error.
If `Resources` is greater than the current number of acquired resources, `Release` returns `Not acquired` error.

Otherwise, the operation succeeds and the value of the semaphore is increased by `Resources`.


### Get

`Get` obtains the current value of the named semaphore.

Request body:

```
 Byte/     0       |       1       |       2       |       3       |
    /              |               |               |               |
   |0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|
   +---------------+---------------+---------------+---------------+
  0| Name length                   | (Name data) ...
   +---------------+---------------+
   Total 2 + (Name length) bytes
```

Success response body:

```
 Byte/     0       |       1       |       2       |       3       |
    /              |               |               |               |
   |0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|
   +---------------+---------------+---------------+---------------+
  0| Resources                                                     |
   +---------------+---------------+---------------+---------------+
   Total 4 bytes
```

- `Name length` is a 2-byte unsigned integer number.
  If this is zero, `Invalid arguments` is returned.

If the named semaphore does not exist, `Get` returns `Not found` error.


### Stats

`Stats` obtains statistics information about semaphores.

Request body: No body.

Successful response body:

The body consists of a series of name-value pairs.

```
 Byte/     0       |       1       |       2       |       3       |
    /              |               |               |               |
   |0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|
   +---------------+---------------+---------------+---------------+
  0| Name length 1                 | Value length 1                |
   +---------------+---------------+---------------+---------------+
  4| Name1 ... Value1 ...
   +---------------+---------------+---------------+---------------+
  N| Name length 2                 | Value length 2                |
   +---------------+---------------+---------------+---------------+
N+4| Name2 ... Value2 ...
   ...
   Total (Body length) bytes.
```

- `Name` is a name of a statistics item.
- `Value` is an ASCII text information.


Configurations
--------------

Some properties of the semaphore extension can be configured by the configuration file.

```ini
# If true, the semaphore extension is enabled. (default: false)
semaphore.enable = false

# TCP port used for the semaphore protocol. (default: 11215)
semaphore.port = 11215

# The maximum number of connections in the semaphore protocol.
# 0 means unlimited. (default: 0)
semaphore.max_connections = 0

# The size of the semaphore hash table. (default: 1000000)
semaphore.buckets = 1000000

# The interval between garbage collections for semaphore objects
# in seconds. (default: 10)
semaphore.gc_interval = 10
```


Implementation
--------------

### gc_thread

Make new class for the semaphore protocol.
Not share the code with existing `gc_thread`.


### Protocol classes

Implement `yrmcds::semaphore::request` and `yrmcds::semaphore::response`.
