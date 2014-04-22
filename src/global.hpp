// Global variables.
// (C) 2014 Cybozu.

#ifndef YRMCDS_GLOBAL_HPP
#define YRMCDS_GLOBAL_HPP

#include <atomic>
#include <ctime>

namespace yrmcds {

extern std::atomic<std::time_t> g_current_time;

} // namespace yrmcds

#endif // YRMCDS_GLOBAL_HPP
