// Utilities
// (C) 2013 Cybozu.

#ifndef CYBOZU_UTIL_HPP
#define CYBOZU_UTIL_HPP

#include <cstddef>
#include <cstring>
#include <endian.h>
#include <string>
#include <system_error>
#include <vector>

namespace cybozu {

// Demangle C++ symbol names.
class demangler {
public:
    // Constructor.
    // @name Mangled symbol name.
    explicit demangler(const char* name);

    // Return the demangled symbol name.
    const std::string& name() const {
        return m_name;
    }

private:
    std::string m_name;
};


// Dump stack trace to the standard error.
void dump_stack() noexcept;


// Throw <std::system_error> for an `errno`.
// @e    `errno` value.
// @func System call name that returns the error number.
inline void throw_unix_error [[noreturn]] (int e, const char* func) {
    dump_stack();
    throw std::system_error(e, std::system_category(), func);
}
// Throw <std::system_error> for an `errno`.
// @e    `errno` value.
// @s    A detailed string to describe the context of the error.
inline void throw_unix_error [[noreturn]] (int e, const std::string& s) {
    dump_stack();
    throw std::system_error(e, std::system_category(), s);
}


// Convert an integer from the network byte order.
// @p   Pointer to a memory region.
// @n   Reference to an integer to store the converted value.
template<typename UInt>
inline void ntoh(const char* p, UInt& n) noexcept {
    UInt d;
    static_assert( sizeof(d) == 2 || sizeof(d) == 4 || sizeof(d) == 8,
                   "Wrong integer type" );
    std::memcpy(&d, p, sizeof(d));
    switch( sizeof(d) ) {
    case 2:
        n = be16toh(d);
        return;
    case 4:
        n = be32toh(d);
        return;
    case 8:
        n = be64toh(d);
        return;
    }
}


// Convert an integer into the network byte order.
// @d   An integer to be converted.
// @p   Pointer to a memory region for the converted integer.
template<typename UInt>
inline void hton(UInt d, char* p) noexcept {
    static_assert( sizeof(d) == 2 || sizeof(d) == 4 || sizeof(d) == 8,
                   "Wrong integer type" );
    UInt n;
    switch( sizeof(d) ) {
    case 2:
        n = htobe16(d);
        std::memcpy(p, &n, 2);
        return;
    case 4:
        n = htobe32(d);
        std::memcpy(p, &n, 4);
        return;
    case 8:
        n = htobe64(d);
        std::memcpy(p, &n, 8);
        return;
    }
}


// Tokenize a string by given delimiter.
// @s  String to be tokenized.
// @c  Delimiter.
//
// This function splits the given string using `c` as a delimiter.
// The result will not contain the delimiter nor empty strings.
//
// @return  A vector of string tokens.
std::vector<std::string> tokenize(const std::string& s, char c);


// Clear memory securely just like memset_s in C11.
// @s  A pointer to a memory region.
// @n  The length of the memory region in bytes.
extern void (* const volatile clear_memory)(void* s, std::size_t n);



} // namespace cybozu

#endif // CYBOZU_UTIL_HPP
