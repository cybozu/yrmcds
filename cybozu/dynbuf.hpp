// A fast, dynamic-sized char buffer.
// (C) 2013 Cybozu.

#ifndef CYBOZU_DYNBUF_HPP
#define CYBOZU_DYNBUF_HPP

#ifdef USE_TCMALLOC
#  include <google/tcmalloc.h>
#else
#  include <cstdlib>
#endif

#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace cybozu {

// A fast, dynamic-sized char buffer.
//
// There are two motivations for this class rather than using <std::vector>
// or <std::string>.  One is to provide nicer interfaces to receive data
// for system calls.  Another is to be efficient as much as possible.
class dynbuf final {
public:
    // Constructor.
    // @default_capacity  The default capacity of the internal buffer.
    //
    // The constructor pre-allocates an internal buffer if `default_capacity`
    // is not 0.  Everytime <reset> is invoked, the internal buffer will be
    // shrunk to `default_capacity`.
    explicit dynbuf(std::size_t default_capacity):
        m_p(default_capacity ? _malloc(default_capacity) : nullptr),
        m_default_capacity(default_capacity),
        m_capacity(default_capacity) {}
    ~dynbuf() {
        if( m_p != nullptr )
            _free(m_p);
    }
    dynbuf(const dynbuf&) = delete;
    dynbuf(dynbuf&& rhs) noexcept:
        m_p(nullptr), m_default_capacity(rhs.m_default_capacity),
        m_capacity(rhs.m_capacity), m_used(rhs.m_used)
    {
        std::swap(m_p, rhs.m_p);
    }
    dynbuf& operator=(const dynbuf&) = delete;
    dynbuf& operator=(dynbuf&&) = delete;

    void swap(dynbuf& other) noexcept {
        std::swap(m_p, other.m_p);
        std::swap(m_capacity, other.m_capacity);
        std::swap(m_used, other.m_used);
    }

    // Clear the contents and reset the internal buffer.
    void reset() {
        m_used = 0;
        if( m_default_capacity == m_capacity )
            return;
        char* new_p = nullptr;
        if( m_default_capacity != 0 )
            new_p = _malloc(m_default_capacity);
        _free(m_p);
        m_p = new_p;
        m_capacity = m_default_capacity;
    }

    // Append contents.
    // @p    The pointer to the contents.
    // @len  The length of the contents.
    void append(const char* p, std::size_t len) {
        if( freebytes() < len )
            enlarge(len);
        std::memcpy(m_p+m_used, p, len);
        m_used += len;
    }

    // Erase contents.
    // @len  The length to be erased.
    //
    // Erase contents from the head of the internal buffer.
    // Remaining data will be moved to the head.
    void erase(std::size_t len) {
        if( m_used < len )
            throw std::invalid_argument("<cybozu::dynbuf::erase>");
        if( len == 0 ) return;
        std::size_t remain = m_used - len;
        if( remain == 0 ) {
            reset();
            return;
        }
        if( remain <= m_default_capacity && m_capacity != m_default_capacity ) {
            char* new_p = _malloc(m_default_capacity);
            std::memcpy(new_p, m_p+len, remain);
            _free(m_p);
            m_p = new_p;
            m_capacity = m_default_capacity;
            m_used = remain;
            return;
        }
        std::memmove(m_p, m_p+len, remain);
        m_used = remain;
    }

    // Prepare enough free space in the internal buffer.
    // @len  Required free space size.
    //
    // This prepares at least `len` byte free space in the internal buffer.
    // The caller should call <consume> after it wrote some data to the
    // free space.
    //
    // @return  The pointer to the free space.
    char* prepare(std::size_t len) {
        if( freebytes() < len )
            enlarge(len);
        return m_p + m_used;
    }

    // Consume the free space.
    // @len  Size of memory to be consumed.
    void consume(std::size_t len) noexcept {
        m_used += len;
    }

    // Return the pointer to the head of the internal buffer.
    const char* data() const noexcept {
        return m_p;
    }

    // Same as STL's `empty()`.
    bool empty() const noexcept {
        return m_used == 0;
    }

    // Same as STL's `size()`.
    std::size_t size() const noexcept {
        return m_used;
    }

private:
    char* m_p;
    const std::size_t m_default_capacity;
    std::size_t m_capacity;
    std::size_t m_used = 0;

    std::size_t freebytes() const noexcept {
        return m_capacity - m_used;
    }

    void enlarge(std::size_t additional) {
        const std::size_t new_capacity = m_capacity + additional;
        m_p = _realloc(m_p, new_capacity);
        m_capacity = new_capacity;
    }

    static char* _malloc(std::size_t len) {
#ifdef USE_TCMALLOC
        char* p = (char*)tc_malloc(len);
#else
        char* p = (char*)std::malloc(len);
#endif
        if( p == nullptr )
            throw std::runtime_error("failed to allocate memory.");
        return p;
    }

    static void _free(void* p) noexcept {
#ifdef USE_TCMALLOC
        tc_free(p);
#else
        std::free(p);
#endif
    }

    static char* _realloc(void* p, std::size_t new_len) {
#ifdef USE_TCMALLOC
        char* np = (char*)tc_realloc(p, new_len);
#else
        char* np = (char*)std::realloc(p, new_len);
#endif
        if( np == nullptr )
            throw std::runtime_error("failed to re-allocate memory.");
        return np;
    }

    friend bool operator==(const dynbuf& lhs, const dynbuf& rhs);
};

inline bool operator==(const dynbuf& lhs, const dynbuf& rhs) {
    if( lhs.m_p == rhs.m_p ) return true;
    if( lhs.m_used != rhs.m_used ) return false;
    return std::memcmp(lhs.m_p, rhs.m_p, lhs.m_used) == 0;
}

} // namespace cybozu

#endif // CYBOZU_DYNBUF_HPP
