// The cache object.
// (C) 2013 Cybozu.

#ifndef YRMCDS_OBJECT_HPP
#define YRMCDS_OBJECT_HPP

#include "stats.hpp"
#include "tempfile.hpp"

#include <cybozu/util.hpp>

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace yrmcds {

// Context for object locking.
//
// The context is in fact the file descriptor of a client connection.
extern thread_local int g_context;


// Object in the hash table.
//
// This class represents an object in the hash table.
// Large objects are stored in temporary files.
class object {
public:
    object(const char* p, std::size_t len,
           std::uint32_t flags_, std::time_t exptime);
    object(std::uint64_t initial, std::time_t exptime);

    // Exception thrown by <incr> or <decr>.
    struct not_a_number: public std::runtime_error {
        not_a_number(): std::runtime_error("") {}
    };

    void set(const char* p, std::size_t len,
             std::uint32_t flags_, std::time_t exptime);
    void append(const char* p, std::size_t len);
    void prepend(const char* p, std::size_t len);
    std::uint64_t incr(std::uint64_t n);
    std::uint64_t decr(std::uint64_t n);
    void touch(std::time_t exptime) {
        m_exptime = exptime;
        m_gc_old = 0;
    }

    const cybozu::dynbuf& data(cybozu::dynbuf& buf) const {
        m_gc_old = 0;
        if( m_file.get() == nullptr ) return m_data;
        buf.reset();
        m_file->read_contents(buf);
        return buf;
    }

    std::size_t size() const noexcept {
        if( m_file.get() == nullptr ) return m_data.size();
        return m_file->length();
    }

    std::uint32_t flags() const noexcept {
        return m_flags;
    }

    std::uint64_t cas_unique() const noexcept {
        return m_cas;
    }

    std::uint32_t exptime() const noexcept {
        return (std::uint32_t)m_exptime;
    }

    bool expired() const noexcept {
        if( locked() ) return false;
        std::time_t t = g_stats.flush_time.load(std::memory_order_relaxed);
        std::time_t now = g_stats.current_time.load(std::memory_order_relaxed);
        if( t != 0 && t <= now ) return true;
        if( m_exptime == 0 ) return false;
        return m_exptime <= now;
    }

    unsigned int age() const noexcept { return m_gc_old; }

    void survive() const {
        ++ m_gc_old;
        if( m_gc_old == FLUSH_AGE && m_file.get() != nullptr )
            m_file->flush();
    }

    void lock() {
        if( locked() )
            throw std::logic_error("object::lock bug");
        m_lock = g_context;
    }

    void unlock(bool force = false) {
        if( ! force && ! locked_by_self() ) {
            cybozu::dump_stack();
            throw std::logic_error("object::unlock bug (m_lock=" +
                                   std::to_string(m_lock) +
                                   ", m_unlocker=" +
                                   std::to_string(m_unlocker) +
                                   ", g_context=" +
                                   std::to_string(g_context) + ")");
        }
        m_unlocker = g_context;
        m_lock = -1;
    }

    // Return `true` if this object is locked.
    bool locked() const noexcept {
        return m_lock != -1;
    }

    // Return `true` if this object is locked by the current context.
    bool locked_by_self() const noexcept {
        return m_lock == g_context;
    }

    // Return `true` if this object is locked by another context.
    bool locked_by_other() const noexcept {
        return locked() && (! locked_by_self());
    }

private:
    std::size_t m_length;
    cybozu::dynbuf m_data;
    std::unique_ptr<tempfile> m_file;
    std::uint32_t m_flags;
    std::time_t m_exptime;
    std::uint64_t m_cas = 1;
    mutable unsigned int m_gc_old = 0;
    int m_lock = -1;
    int m_unlocker = -2;
};

} // namespace yrmcds

#endif // YRMCDS_OBJECT_HPP
