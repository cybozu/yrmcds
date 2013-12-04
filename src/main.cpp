// The entry point of yrmcdsd.
// (C) 2013 Cybozu.

#include "config.hpp"
#include "constants.hpp"
#include "server.hpp"

#include <cybozu/filesystem.hpp>
#include <cybozu/siphash.hpp>
#include <cybozu/util.hpp>

#include <algorithm>
#include <grp.h>
#include <iostream>
#include <pwd.h>
#include <random>
#include <string>
#include <sys/prctl.h>
#include <sys/types.h>
#include <typeinfo>
#include <unistd.h>
#include <vector>

#define QUOTE(str) #str
#define EXPAND_AND_QUOTE(str) QUOTE(str)

namespace {

#include "../COPYING.hpp"

void print_help() {
    std::cout << "Usage: yrmcdsd [-v] [-h] [-f FILE]" << std::endl;
}

void print_version() {
    std::cout << yrmcds::VERSION << std::endl << std::endl << COPYING;
}

void seed_siphash() {
    union {
        char key[16];
        std::uint64_t ikey[2];
    } k;

    std::random_device rd;
    std::uniform_int_distribution<std::uint64_t> dis;
    k.ikey[0] = dis(rd);
    k.ikey[1] = dis(rd);
    cybozu::siphash24_seed(k.key);
}

bool load_config(const std::vector<std::string>& args) {
    auto it = std::find(args.begin(), args.end(), "-f");
    if( it != args.end() ) {
        ++it;
        if( it == args.end() ) {
            std::cerr << "missing filename after -f" << std::endl;
            return false;
        }
        yrmcds::g_config.load(*it);
    } else {
        std::string config_path = EXPAND_AND_QUOTE(DEFAULT_CONFIG);
        struct stat st;
        if( cybozu::get_stat(config_path, st) )
            yrmcds::g_config.load( config_path );
    }
    return true;
}

} // anonymous namespace

int main(int argc, char** argv) {
    std::vector<std::string> args;
    for( int i = 1; i < argc; ++i )
        args.push_back(argv[i]);

    if( std::find(args.begin(), args.end(), "-h") != args.end() ) {
        print_help();
        return 0;
    }

    if( std::find(args.begin(), args.end(), "-v") != args.end() ) {
        print_version();
        return 0;
    }

    using cybozu::logger;

    seed_siphash();

    try {
        if( ! load_config(args) )
            return 1;

        // first, set group id (as root)
        const std::string& g = yrmcds::g_config.group();
        if( ! g.empty() ) {
            if( getuid() != 0 ) {
                std::cerr << "Ignored group configuration." << std::endl;
            } else {
                struct group* grp = getgrnam(g.c_str());
                if( grp == nullptr ) {
                    std::cerr << "No such group: " << g << std::endl;
                    return 1;
                }
                if( setgid(grp->gr_gid) == -1 ) {
                    std::cerr << "Failed to set group id!" << std::endl;
                    return 1;
                }
            }
        }

        const std::string& u = yrmcds::g_config.user();
        if( ! u.empty() ) {
            if( getuid() != 0 ) {
                std::cerr << "Ignored user configuration." << std::endl;
            } else {
                struct passwd* p = getpwnam(u.c_str());
                if( p == nullptr ) {
                    std::cerr << "No such user: " << u << std::endl;
                    return 1;
                }
                if( setuid(p->pw_uid) == -1 ) {
                    std::cerr << "Failed to set user id!" << std::endl;
                    return 1;
                }
            }
        }

        // make it core dumpable
        if( prctl(PR_SET_DUMPABLE, 1) == -1 ) {
            std::cerr << "WARNING: failed to enable core dump!" << std::endl;
        }

        // setup logger
        logger::set_threshold(yrmcds::g_config.threshold());
        if( ! yrmcds::g_config.logfile().empty() )
            logger::instance().open(yrmcds::g_config.logfile());

        yrmcds::server().serve();

    } catch( const std::system_error& e ) {
        cybozu::demangler t( typeid(e).name() );
        logger::error() << "[" << t.name() << "] (" << e.code() << ") "
                        << e.what();
        std::cerr << "Exception [" << t.name() << "] (" << e.code() << ") "
                  << e.what() << std::endl;
        return 1;

    } catch( const std::exception& e ) {
        cybozu::demangler t( typeid(e).name() );
        logger::error() << "[" << t.name() << "] " << e.what();
        std::cerr << "Exception [" << t.name() << "] " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
