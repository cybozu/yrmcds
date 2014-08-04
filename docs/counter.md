#<cldoc:Counter support>

Counter support

Overview
--------

Counter extension provides distributed counters for the resource management.  Values of counters represent the consumption of some resouces.  Multiple clients can acquire/release the resources atomically.

Each counter has a non-empty name.  The maximum name is up to 65535 bytes.  The number of resources available for a counter is represented as a 4 byte unsigned integer; this means a counter can have up to 4294967295 resources.

- The counter extension is disabled by default.  It can be enabled by a configuration file.
- TCP port used for the counter protocol is different from that of the memcache protocol.
- The namespace of counter objects is different from that of the memcache objects.
- When a connection is closed, all counter objects acquired by the connection are released automatically.
- A counter is created dynamically at the first time the counter is acquired.
- Counters will be deleted by the GC thread if all resources managed by them are released.
- Counters are not replicated.


Protocol
--------

The counter extension has its own binary protocol.

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
| `0x11` | `Dump`


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

`Acquire` tries to acquire resources of a counter.  If the consumption of resources after `Acquire` is less than or equal to the given maximum value, `Acquire` will success.  Otherwise, `Acquire` will fail and no resouces are acquired.

Request body:

```
 Byte/     0       |       1       |       2       |       3       |
    /              |               |               |               |
   |0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|
   +---------------+---------------+---------------+---------------+
  0| Resources                                                     |
   +---------------+---------------+---------------+---------------+
  4| Maximum                                                       |
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
- `Maximum` is a 4-byte unsigned integer number.
- This parameter represents the maximum number of resources.
  `Maximum` must be equal to or greater than `Resources`, otherwise
  `Invalid arguments` is returned.
- `Name length` is a 2-byte unsigned integer number.
  If this is zero, `Invalid arguments` is returned.

If the named counter does not exist, `Acquire` creates a new counter, sets its consumption to be `Resources`, and returns the success.

Otherwise, if `Consumption + Resources < Maximum` where `Consumption` is the current consumption of the resource, then this request will augment `Consumption` by `Resources`, and return the success.  Else, this returns `Resource not available` error.


### Release

`Release` returns the acquired resources back.

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

If the named counter does not exist, `Release` returns `Not found` error.
If `Resources` is greater than the current number of acquired resources, `Release` returns `Not acquired` error.

Otherwise, the operation succeeds and the value of the counter is increased by `Resources`.


### Get

`Get` obtains the current consumption of a counter.

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
  0| Consumption                                                   |
   +---------------+---------------+---------------+---------------+
   Total 4 bytes
```

- `Name length` is a 2-byte unsigned integer number.
  If this is zero, `Invalid arguments` is returned.

If the named counter does not exist, `Get` returns `Not found` error.


### Stats

`Stats` obtains statistics information about counters.

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


### Dump

`Dump` dumps all counters.

Request body: No body.

Successful responses:

`Dump` command returns multiple responses for one request.

Each response contains the name of a counter and the current consumption of the resource.
The response also contains the maximum consumption of resources in the interval given in the configuration file.

The end of the series of responses are indicated by the response whose body length is zero.

```
 Byte/     0       |       1       |       2       |       3       |
    /              |               |               |               |
   |0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|
   +---------------+---------------+---------------+---------------+
  0| Current consumption                                           |
   +---------------+---------------+---------------+---------------+
  4| Reserved                                                      |
   +---------------+---------------+---------------+---------------+
  8| Maximum consumption                                           |
   +---------------+---------------+---------------+---------------+
 12| Name length                   | (Name data) ...
   +---------------+---------------+
   Total 14 + (Name length) bytes
```


Configurations
--------------

Some properties of the counter extension can be configured by the configuration file.

```ini
# If true, the counter extension is enabled. (default: false)
counter.enable = false

# TCP port used for the counter protocol. (default: 11215)
counter.port = 11215

# The maximum number of connections in the counter protocol.
# 0 means unlimited. (default: 0)
counter.max_connections = 0

# The size of the counter hash table. (default: 1000000)
counter.buckets = 1000000

# The interval of measuring the maximum number of resource consumption
# in seconds. (default: 86400)
counter.consumption_stats.interval = 86400
```
