// filesystem.hpp
// (C) 2013 Cybozu.

#ifndef CYBOZU_FILESYSTEM_HPP
#define CYBOZU_FILESYSTEM_HPP

#include "util.hpp"

#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace cybozu {

// stat(2) wrapper.
// @return `true` if a file exists at `path`, `false` otherwise.
bool get_stat(const std::string& path, struct stat& st);

inline bool is_dir(const std::string& path) {
    struct stat st;
    if( ! get_stat(path, st) ) return false;
    return S_ISDIR(st.st_mode);
}

inline bool is_readable(const std::string& path) {
    return ::access(path.c_str(), R_OK) == 0;
}

inline bool is_writable(const std::string& path) {
    return ::access(path.c_str(), W_OK) == 0;
}

} // namespace cybozu

#endif // CYBOZU_FILESYSTEM_HPP
