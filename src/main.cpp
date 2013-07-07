// The entry point of yrmcdsd.
// (C) 2013 Cybozu.

#include "config.hpp"
#include "server.hpp"

#include <cybozu/filesystem.hpp>
#include <cybozu/util.hpp>

#include <algorithm>
#include <iostream>
#include <string>
#include <typeinfo>
#include <vector>

#define QUOTE(str) #str
#define EXPAND_AND_QUOTE(str) QUOTE(str)

namespace {

void print_help() {
    std::cout << "Usage: yrmcdsd [-h] [-f FILE]" << std::endl; 
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

    using cybozu::logger;

    try {
        if( ! load_config(args) )
            return 1;

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
