// The object hash map.
// (C) 2013 Cybozu.

#ifndef CYBOZU_HASH_MAP_HPP
#define CYBOZU_HASH_MAP_HPP

#include "MurmurHash3.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace cybozu {

// Key class for <hash_map>.
class hash_key final {
public:
    // Construct from a statically allocated memory.
    // @p   Pointer to a statically allocated memory.
    // @len Length of the key.
    //
    // Construct from a statically allocated memory.
    //
    // As long as the constructed object lives, the memory pointed by `p`
    // must not be freed.
    hash_key(const char* p, std::size_t len) noexcept:
        m_p(p), m_len(len) {
        MurmurHash3_x86_32(m_p, (int)m_len, 0, &m_hash);
    }

    // Construct by moving a <std::vector>.
    //
    // Construct by moving a <std::vector>.  Sample usage:
    // ```
    // hash_key( std::vector<char>(p, p+len) )
    // ```
    hash_key(std::vector<char> v):
        m_v(std::move(v)), m_p(m_v.data()), m_len(m_v.size()) {
        MurmurHash3_x86_32(m_p, (int)m_len, 0, &m_hash);
    }

    // Copy constructor.
    hash_key(const hash_key& rhs):
        m_v(rhs.m_p, rhs.m_p+rhs.m_len), m_p(m_v.data()), m_len(m_v.size()),
        m_hash(rhs.m_hash) {}
    hash_key& operator=(const hash_key& rhs) = delete;

    // Move contructor and assign operator.
    hash_key(hash_key&& rhs) noexcept = default;
    hash_key& operator=(hash_key&& rhs) = default;

    std::uint32_t hash() const noexcept {
        return m_hash;
    }

    const char* data() const noexcept {
        return m_p;
    }

    std::size_t length() const noexcept {
        return m_len;
    }

    std::string str() const {
        return std::string(m_p, m_len);
    }

private:
    std::vector<char> m_v;
    const char* m_p;
    std::size_t m_len;
    std::uint32_t m_hash;

    friend bool operator==(const hash_key&, const hash_key&) noexcept;
};

inline bool operator==(const hash_key& lhs, const hash_key& rhs) noexcept {
    if( lhs.m_len != rhs.m_len ) return false;
    if( lhs.m_p == rhs.m_p ) return true;
    return std::memcmp(lhs.m_p, rhs.m_p, lhs.m_len) == 0;
}


// Return the nearest prime number.
unsigned int nearest_prime(unsigned int n) noexcept;


// Highly concurrent object hash map.
//
// Keys for this hash map are <hash_key> whereas objects are of type `T`.
//
// Each bucket in the hash map has its unique mutex to guard itself.
// This design reduces contensions between threads drastically in exchange
// for some functions such as dynamic resizing of the number of buckets.
template<typename T>
class hash_map {
public:
    // Hash map bucket.
    //
    // Each hash value corresponds to a bucket.
    // Member functions whose names end with `_nolock` are not thread-safe.
    class bucket {
    public:
        bucket() {
            m_objects.reserve(2);
        }

        // Handle or insert an object.
        // @key      The object's key.
        // @handler  A function to handle an existing object.
        // @creator  A function to create a new object.
        //
        // This function can be used to handle an existing object, or
        // to insert a new object when such an object does not exist.
        //
        // If `handler` is not `nullptr` and there is no existing object
        // for `key`, `false` is returned.  If `creator` is not `nullptr`
        // and an object for `key` exists, `false` is returned.
        //
        // `handler` can return `false` if it failed to handle the object.
        // Otherwise, `handler` should return `true`.
        //
        // @return `true` if succeeded, `false` otherwise.
        bool apply_nolock(const hash_key& key,
                          std::function<bool(const hash_key&, T&)> handler,
                          std::function<std::unique_ptr<T>(const hash_key&)> creator) {
            for( item& t: m_objects ) {
                const hash_key& t_key = std::get<0>(t);
                if( t_key == key ) {
                    if( ! handler ) return false;
                    return handler(t_key, *std::get<1>(t));
                }
            }
            if( ! creator ) return false;
            m_objects.emplace_back(key, creator(key));
            return true;
        }

        // Thread-safe <apply_nolock>.
        bool apply(const hash_key& key,
                   std::function<bool(const hash_key&, T&)> handler,
                   std::function<std::unique_ptr<T>(const hash_key&)> creator) {
            lock_guard g(m_lock);
            return apply_nolock(key, handler, creator);
        }

        // Remove an object for `key`.
        // @key       The object's key.
        // @callback  A function called when an object is removed.
        //
        // This removes an object associated with `key`.  If there is no
        // object associated with `key`, return `false`.  If `callback`
        // is not `nullptr`, it is called when an object is removed.
        //
        // @return `true` if successfully removed, `false` otherwise.
        bool remove_nolock(const hash_key& key,
                           std::function<void(const hash_key&)> callback) {
            for( auto it = m_objects.begin(); it != m_objects.end(); ++it) {
                if( std::get<0>(*it) == key ) {
                    m_objects.erase(it);
                    if( callback ) callback(key);
                    return true;
                }
            }
            return false;
        }

        // Thread-safe <remove_nolock>.
        bool remove(const hash_key& key,
                    std::function<void(const hash_key&)> callback) {
            lock_guard g(m_lock);
            return remove_nolock(key, callback);
        }

        // Remove an object for `key` if `pred` returns `true`.
        // @key   The object's key.
        // @pred  A predicate function.
        //
        // This function removes an object assiciated with `key` if a
        // predicate function returns `true`.  This function is thread-safe.
        //
        // @return `true` if object existed, `false` otherwise.
        bool remove_if(const hash_key& key,
                       std::function<bool(const hash_key&, T&)> pred) {
            lock_guard g(m_lock);
            for( auto it = m_objects.begin(); it != m_objects.end(); ++it) {
                if( std::get<0>(*it) == key ) {
                    if( pred(key, *std::get<1>(*it)) )
                        m_objects.erase(it);
                    return true;
                }
            }
            return false;
        }

        // Collect garbage objects.
        // @pred      Predicate function.
        //
        // This function collects garbage objects.
        // Objects for which `pred` returns `true` will be removed.
        void gc(std::function<bool(const hash_key&, T&)> pred) {
            lock_guard g(m_lock);
            for( auto it = m_objects.begin(); it != m_objects.end(); ) {
                if( pred(std::get<0>(*it), *std::get<1>(*it)) ) {
                    it = m_objects.erase(it);
                } else {
                    ++it;
                }
            }
        }

        // Clear objects in this bucket.
        void clear_nolock() {
            m_objects.clear();
        }
    private:
        using item = std::tuple<hash_key, std::unique_ptr<T>>;
        using lock_guard = std::lock_guard<std::mutex>;
        alignas(CACHELINE_SIZE) mutable std::mutex m_lock;
        std::vector<item> m_objects;
    };

    hash_map(unsigned int buckets):
        m_size(nearest_prime(buckets)), m_buckets(m_size) {}

    typedef std::function<bool(const hash_key&, T&)> handler;
    typedef std::function<std::unique_ptr<T>(const hash_key&)> creator;

    // Handle or insert an object.
    // @key      The object's key.
    // @h        A function to handle an existing object.
    // @c        A function to create a new object.
    //
    // This function can be used to handle an existing object, or
    // to insert a new object when such an object does not exist.
    //
    // If `handler` is not `nullptr` and there is no existing object
    // for `key`, `false` is returned.  If `creator` is not `nullptr`
    // and an object for `key` exists, `false` is returned.
    //
    // `handler` can return `false` if it failed to handle the object.
    // Otherwise, `handler` should return `true`.
    //
    // @return `true` if succeeded, `false` otherwise.
    bool apply_nolock(const hash_key& key, handler h, creator c) {
        return get_bucket(key).apply_nolock(key, h, c);
    }

    // Thread-safe <apply_nolock>.
    bool apply(const hash_key& key, handler h, creator c) {
        return get_bucket(key).apply(key, h, c);
    }

    // Remove an object for `key`.
    // @key       The object's key.
    // @callback  A function called when an object is removed.
    //
    // This removes an object associated with `key`.  If there is no
    // object associated with `key`, return `false`.  If `callback`
    // is not `nullptr`, it is called when an object is removed.
    //
    // @return `true` if successfully removed, `false` otherwise.
    bool remove_nolock(const hash_key& key,
                       std::function<void(const hash_key&)> callback) {
        return get_bucket(key).remove_nolock(key, callback);
    }

    // Thread-safe <remove_nolock>.
    bool remove(const hash_key& key,
                std::function<void(const hash_key&)> callback) {
        return get_bucket(key).remove(key, callback);
    }

    // Remove an object for `key` if `pred` returns `true`.
    // @key   The object's key.
    // @pred  A predicate function.
    //
    // This function removes an object assiciated with `key` if a
    // predicate function returns `true`.  This function is thread-safe.
    //
    // @return `true` if object existed, `false` otherwise.
    bool remove_if(const hash_key& key,
                   std::function<bool(const hash_key&, T&)> pred) {
        return get_bucket(key).remove_if(key, pred);
    }

    /* Bucket interfaces */

    // Return the number of buckets in this hash map.
    std::size_t bucket_count() const noexcept {
        return m_size;
    }

    bucket& get_bucket(const hash_key& key) noexcept {
        return m_buckets[key.hash() % m_size];
    }

    // <bucket> iterator.
    using iterator = typename std::vector<bucket>::iterator;

    iterator begin() {
        return m_buckets.begin();
    }

    iterator end() {
        return m_buckets.end();
    }

private:
    const std::size_t   m_size;
    std::vector<bucket> m_buckets;
};

} // namespace cybozu

#endif // CYBOZU_HASH_MAP_HPP
