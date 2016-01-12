#<cldoc:ExtKeys>

Key dump extesion
=================

yrmcds provides an extension to memcached protocol to dump existing keys.

Text protocol
-------------

The general description about the text protocol is available at:
https://github.com/memcached/memcached/blob/master/doc/protocol.txt

The key dump command syntax is:

    keys [<prefix>]\r\n

where `prefix` is the prefix string of dumping keys.
If `prefix` is omitted, all keys will be dumped.

After this command, the client expects zero or more values, each of
which is received as a text line.  After all the items have been
transmitted, the server sends the string

    END\r\n

to indicate the end of response.

Each item sent by the server looks like this:

    VALUE <key>\r\n

where `key` is a key having the given prefix.

Binary protocol
---------------

The general description about the binary protocol is available at:
https://code.google.com/p/memcached/wiki/MemcacheBinaryProtocol

This extension introduces a new opcode:

    0x50 (Keys)

`Keys` Request:

    - MUST NOT have extras.
    - MAY have key.
    - MUST NOT have value.

Successful response of `Keys` consists of a series of these responses:

    - MUST NOT have extras.
    - MUST have key.
    - MUST NOT have value.

followed by an empty response (i.e., key length is 0).
