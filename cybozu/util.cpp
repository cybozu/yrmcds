// (C) 2013 Cybozu.

#include "util.hpp"

#include <cxxabi.h>
#include <cstddef>
#include <cstdlib>
#include <ctime>
#include <execinfo.h>
#include <sstream>
#include <string.h>
#include <stdexcept>
#include <unistd.h>

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

void dump_stack() noexcept {
    char buf[32];
    std::time_t t = std::time(nullptr);
    std::size_t len = std::strlen(ctime_r(&t, buf));
    write(STDERR_FILENO, buf, len);

    void* bt[100];
    int n = backtrace(bt, 100);
    backtrace_symbols_fd(bt, n, STDERR_FILENO);
}

} // namespace cybozu
