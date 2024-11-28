// (C) 2024 Cybozu.

#include "election.hpp"
#include "config.hpp"

#include <filesystem>

namespace fs = std::filesystem;

namespace yrmcds {

bool is_master() {
    auto method = g_config.leader_election_method();
    if( method == leader_election_method::virtual_ip ) {
        auto vip = g_config.vip();
        return cybozu::has_ip_address(*vip);
    } else if( method == leader_election_method::file ) {
        auto& path = *g_config.master_file_path();
        return fs::exists(path);
    } else {
        throw std::runtime_error("Invalid leader_election_method");
    }
}

} // namespace yrmcds
