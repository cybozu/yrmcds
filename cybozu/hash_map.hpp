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
#include <type_traits>
#include <utility>
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
// `T` must be either move-constructible or copyable.
//
// Each bucket in the hash map has its unique mutex to guard itself.
// This design reduces contensions between threads drastically in exchange
// for some functions such as dynamic resizing of the number of buckets.
template<typename T>
class hash_map {
    static_assert( std::is_move_constructible<T>::value ||
                   std::is_copy_constructible<T>::value,
                   "T must be move- or copy- constructible." );

public:
    typedef std::function<bool(const hash_key&, T&)> handler;
    typedef std::function<T(const hash_key&)> creator;

    // Hash map bucket.
    //
    // Each hash value corresponds to a bucket.
    // Member functions whose names end with `_nolock` are not thread-safe.
    class bucket {

        struct item {
            const hash_key key;
            T object;
            item* next;

            // perfect forwarding
            template<typename X>
            item(const hash_key& k, X&& o, item* n):
                key(k), object(std::forward<X>(o)), next(n) {}
        };

    public:
        bucket(): m_objects(nullptr) {}

        // Handle or insert an object.
        // @key  The object's key.
        // @h    A function to handle an existing object.
        // @c    A function to create a new object.
        //
        // This function can be used to handle an existing object, or
        // to insert a new object when such an object does not exist.
        //
        // If `h` is not `nullptr` and there is no existing object for
        // `key`, `false` is returned.  If `c` is not `nullptr` and an
        // object for `key` exists, `false` is returned.
        //
        // `h` can return `false` if it failed to handle the object.
        // Otherwise, `h` should return `true`.
        //
        // @return `true` if succeeded, `false` otherwise.
        bool apply_nolock(const hash_key& key,
                          const handler& h, const creator& c) {
            for( item* p = m_objects; p != nullptr; p = p->next ) {
                if( p->key == key ) {
                    if( ! h ) return false;
                    return h(p->key, p->object);
                }
            }
            if( ! c ) return false;
            m_objects = new item(key, c(key), m_objects);
            return true;
        }

        // Thread-safe <apply_nolock>.
        bool apply(const hash_key& key, const handler& h, const creator& c) {
            lock_guard g(m_lock);
            return apply_nolock(key, h, c);
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
                           const std::function<void(const hash_key&)>& callback) {
            for( item** p = &m_objects; *p != nullptr; p = &((*p)->next) ) {
                if( (*p)->key == key ) {
                    item* to_delete = *p;
                    *p = to_delete->next;
                    delete to_delete;
                    if( callback ) callback(key);
                    return true;
                }
            }
            return false;
        }

        // Thread-safe <remove_nolock>.
        bool remove(const hash_key& key,
                    const std::function<void(const hash_key&)>& callback) {
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
                       const std::function<bool(const hash_key&, T&)>& pred) {
            lock_guard g(m_lock);
            for( item** p = &m_objects; *p != nullptr; p = &((*p)->next) ) {
                item* to_delete = *p;
                if( to_delete->key == key ) {
                    if( pred(key, to_delete->object) ) {
                        *p = to_delete->next;
                        delete to_delete;
                    }
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
        void gc(const std::function<bool(const hash_key&, T&)>& pred) {
            lock_guard g(m_lock);
            for( item** p = &m_objects; *p != nullptr; ) {
                item* to_delete = *p;
                if( pred(to_delete->key, to_delete->object) ) {
                    *p = to_delete->next;
                    delete to_delete;
                } else {
                    p = &(to_delete->next);
                }
            }
        }

        // Clear objects in this bucket.
        void clear_nolock() {
            while( m_objects ) {
                item* next = m_objects->next;
                delete m_objects;
                m_objects = next;
            }
        }
    private:
        using lock_guard = std::lock_guard<std::mutex>;
        alignas(CACHELINE_SIZE)
        mutable std::mutex m_lock;
        item* m_objects;
    };

    hash_map(unsigned int buckets):
        m_size(nearest_prime(buckets)), m_buckets(m_size) {}

    // Handle or insert an object.
    // @key      The object's key.
    // @h        A function to handle an existing object.
    // @c        A function to create a new object.
    //
    // This function can be used to handle an existing object, or
    // to insert a new object when such an object does not exist.
    //
    // If `h` is not `nullptr` and there is no existing object for
    // `key`, `false` is returned.  If `c` is not `nullptr` and an
    // object for `key` exists, `false` is returned.
    //
    // `h` can return `false` if it failed to handle the object.
    // Otherwise, `h` should return `true`.
    //
    // @return `true` if succeeded, `false` otherwise.
    bool apply_nolock(const hash_key& key,
                      const handler& h, const creator& c) {
        return get_bucket(key).apply_nolock(key, h, c);
    }

    // Thread-safe <apply_nolock>.
    bool apply(const hash_key& key, const handler& h, const creator& c) {
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
                       const std::function<void(const hash_key&)>& callback) {
        return get_bucket(key).remove_nolock(key, callback);
    }

    // Thread-safe <remove_nolock>.
    bool remove(const hash_key& key,
                const std::function<void(const hash_key&)>& callback) {
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
                   const std::function<bool(const hash_key&, T&)>& pred) {
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
