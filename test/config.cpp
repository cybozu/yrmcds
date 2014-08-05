#include "../src/config.hpp"

#include <cybozu/test.hpp>

const std::string TEST_CONF = "test/test.conf";

AUTOTEST(config) {
    using yrmcds::g_config;
    g_config.load(TEST_CONF);
    cybozu_assert(g_config.port() == 1121);
    cybozu_assert(g_config.repl_port() == 1122);
    cybozu_assert(g_config.max_connections() == 10000);
    cybozu_assert(g_config.user() == "nobody");
    cybozu_assert(g_config.group() == "nogroup");
    cybozu_assert(g_config.memory_limit() == (1024 << 20));
    cybozu_assert(g_config.threshold() == cybozu::severity::warning);
    cybozu_assert(g_config.max_data_size() == (5 << 20));
    cybozu_assert(g_config.heap_data_limit() == (16 << 10));
    cybozu_assert(g_config.workers() == 10);
    cybozu_assert(g_config.gc_interval() == 20);
    cybozu_assert(g_config.counter().enable() == true);
    cybozu_assert(g_config.counter().port() == 11216);
    cybozu_assert(g_config.counter().max_connections() == 100);
    cybozu_assert(g_config.counter().buckets() == 1000001);
    cybozu_assert(g_config.counter().consumption_stats_interval() == 12345);
}
