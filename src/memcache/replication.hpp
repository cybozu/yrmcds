// Replication protocol.
// (C) 2013 Cybozu.

#ifndef YRMCDS_MEMCACHE_REPLICATION_HPP
#define YRMCDS_MEMCACHE_REPLICATION_HPP

#include "object.hpp"
#include "sockets.hpp"

#include <cybozu/hash_map.hpp>

#include <vector>

namespace yrmcds { namespace memcache {

void repl_object(const std::vector<repl_socket*>& slaves,
                 const cybozu::hash_key& key, const object& obj,
                 bool flush = true);

void repl_touch(const std::vector<repl_socket*>& slaves,
                const cybozu::hash_key& key, const object& obj);

void repl_delete(const std::vector<repl_socket*>& slaves,
                 const cybozu::hash_key& key);

std::size_t repl_recv(const char* p, std::size_t len,
                      cybozu::hash_map<object>& hash);

}} // namespace yrmcds::memcache

#endif // YRMCDS_MEMCACHE_REPLICATION_HPP
