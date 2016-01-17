// (C) 2013 Cybozu.

#include "util.hpp"

#include <cxxabi.h>
#include <cstddef>
#include <cstdlib>
#include <ctime>
#include <execinfo.h>
#include <iostream>
#include <sstream>
#include <string.h>
#include <stdexcept>
#include <unistd.h>

namespace {

void clear_memory_(void* s, std::size_t n) {
    volatile unsigned char *p = (unsigned char*)s;
    while( n-- ) *p++ = 0;
}

} // anonymous namespace

namespace cybozu {

demangler::demangler(const char* name) {
    int status;
    char* demangled = abi::__cxa_demangle(name, 0, 0, &status);
    if( status == 0 ) {
        m_name = demangled;
        std::free(demangled);
    } else {
        m_name = name;
    }
}

#pragma GCC diagnostic ignored "-Wunused-result"
void dump_stack() noexcept {
    char buf[32];
    std::time_t t = std::time(nullptr);
    std::size_t len = std::strlen(ctime_r(&t, buf));
    ::write(STDERR_FILENO, buf, len);

    void* bt[100];
    int n = backtrace(bt, 100);
    backtrace_symbols_fd(bt, n, STDERR_FILENO);
}
#pragma GCC diagnostic pop

std::vector<std::string> tokenize(const std::string& s, char c) {
    auto end = s.cend();
    auto start = end;

    std::vector<std::string> v;
    for( auto it = s.cbegin(); it != end; ++it ) {
        if( *it != c ) {
            if( start == end )
                start = it;
            continue;
        }
        if( start != end ) {
            v.emplace_back(start, it);
            start = end;
        }
    }
    if( start != end )
        v.emplace_back(start, end);
    return std::move(v);
}

void (* const volatile clear_memory)(void* s, std::size_t n) = clear_memory_;

} // namespace cybozu
