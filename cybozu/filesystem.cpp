// (C) 2013 Cybozu.

#include "filesystem.hpp"
#include "logger.hpp"

#include <cerrno>
#include <system_error>

namespace cybozu {

bool get_stat(const std::string& path, struct stat& st) {
    if( ::stat(path.c_str(), &st) == 0 )
        return true;
    if( errno == EFAULT || errno == ENOMEM )
        throw_unix_error(errno, "stat");
    auto ec = std::system_category().default_error_condition(3);
    logger::debug() << "stat failed. path="
                    << path << ", error=" << ec.message()
                    << " (" << errno << ")";
    return false;
}

} // namespace cybozu
