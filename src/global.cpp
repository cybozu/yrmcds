// (C) 2014 Cybozu.

#include "global.hpp"

namespace yrmcds {

std::atomic<std::time_t> g_current_time(std::time(nullptr));

} // namespace yrmcds
