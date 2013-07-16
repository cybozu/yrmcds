// (C) 2013 Cybozu.

#include "config.hpp"
#include "object.hpp"
#include "stats.hpp"

#include <cstdint>
#include <limits>
#include <stdio.h>
#include <string>

namespace {

std::uint64_t to_uint64(const char* p, std::size_t len) {
    unsigned long long ull = std::stoull( std::string(p, len) );
    if( ull > std::numeric_limits<std::uint64_t>::max() )
        throw std::out_of_range("out of range for uint64");
    return static_cast<std::uint64_t>(ull);
}

} // anonymous namespace

namespace yrmcds {

thread_local int g_context = -1;

object::object(const char* p, std::size_t len,
               std::uint32_t flags_, std::time_t exptime):
    m_length(len), m_data(0), m_file(nullptr),
    m_flags(flags_), m_exptime(exptime) {
    if( len > g_config.heap_data_limit() ) {
        m_file = std::unique_ptr<tempfile>(new tempfile);
        m_file->write(p, len);
    } else {
        if( len > 0 )
            m_data.append(p, len);
    }
    g_stats.total_objects.fetch_add(1, std::memory_order_relaxed);
}

object::object(std::uint64_t initial, std::time_t exptime):
    m_length(0), m_data(24), m_file(nullptr), m_flags(0), m_exptime(exptime) {
    char s_value[24]; // uint64 can be as large as 20 byte decimal string.
    m_length = ::snprintf(s_value, sizeof(s_value),
                          "%llu", (unsigned long long)initial);
    m_data.append(s_value, m_length);
    g_stats.total_objects.fetch_add(1, std::memory_order_relaxed);
}

void object::set(const char* p, std::size_t len,
                 std::uint32_t flags_, std::time_t exptime) {
    m_flags = flags_;
    m_exptime = exptime;
    ++ m_cas;
    m_gc_old = 0;
    m_data.reset();

    if( len > g_config.heap_data_limit() ) {
        if( m_file.get() == nullptr ) {
            m_file = std::unique_ptr<tempfile>(new tempfile);
        } else {
            m_file->clear();
        }
        m_file->write(p, len);
    } else {
        m_file = nullptr;
        if( len > 0 )
            m_data.append(p, len);
    }
    m_length = len;
}

void object::append(const char* p, std::size_t len) {
    ++ m_cas;
    m_gc_old = 0;
    if( len == 0 ) return;

    std::size_t new_size = m_length + len;
    if( new_size > g_config.heap_data_limit() ) {
        if( m_file.get() == nullptr ) {
            m_file = std::unique_ptr<tempfile>(new tempfile);
            if( m_length > 0 )
                m_file->write(m_data.data(), m_length);
            m_data.reset();
            m_file->write(p, len);
        } else {
            m_file->write(p, len);
        }
    } else {
        m_data.append(p, len);
    }
    m_length = new_size;
}

void object::prepend(const char* p, std::size_t len) {
    ++ m_cas;
    m_gc_old = 0;
    if( len == 0 ) return;

    std::size_t new_size = m_length + len;
    if( new_size > g_config.heap_data_limit() ) {
        if( m_file.get() == nullptr ) {
            m_file = std::unique_ptr<tempfile>(new tempfile);
            m_file->write(p, len);
            if( m_length > 0 )
                m_file->write(m_data.data(), m_length);
            m_data.reset();
        } else {
            cybozu::dynbuf buf(new_size);
            buf.append(p, len);
            m_file->read_contents(buf);
            m_file->clear();
            m_file->write(buf.data(), new_size);
        }
    } else {
        cybozu::dynbuf buf(new_size);
        buf.append(p, len);
        buf.append(m_data.data(), m_length);
        m_data.swap(buf);
    }
    m_length = new_size;
}

std::uint64_t object::incr(std::uint64_t n) {
    if( m_file.get() != nullptr )
        throw not_a_number{};
    std::uint64_t u64_value;
    try {
        u64_value = to_uint64(m_data.data(), m_length);
    } catch( const std::logic_error& ) {
        throw not_a_number{};
    }
    u64_value += n;
    char s_value[24]; // uint64 can be as large as 20 byte decimal string.
    m_length = ::snprintf(s_value, sizeof(s_value),
                          "%llu", (unsigned long long)u64_value);
    m_data.reset();
    m_data.append(s_value, m_length);
    ++ m_cas;
    m_gc_old = 0;
    return u64_value;
}

std::uint64_t object::decr(std::uint64_t n) {
    if( m_file.get() != nullptr )
        throw not_a_number{};
    std::uint64_t u64_value;
    try {
        u64_value = to_uint64(m_data.data(), m_length);
    } catch( const std::logic_error& ) {
        throw not_a_number{};
    }
    u64_value = (u64_value < n) ? 0 : (u64_value - n);
    char s_value[24]; // uint64 can be as large as 20 byte decimal string.
    m_length = ::snprintf(s_value, sizeof(s_value),
                          "%llu", (unsigned long long)u64_value);
    m_data.reset();
    m_data.append(s_value, m_length);
    ++ m_cas;
    m_gc_old = 0;
    return u64_value;
}

} // namespace yrmcds
