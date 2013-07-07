// (C) 2013 Cybozu.

#include "config_parser.hpp"

#include <fstream>

namespace {

const char white_spaces[] = " \f\n\r\t\v";

inline void rtrim(std::string& s) {
    s.erase( s.find_last_not_of(white_spaces) + 1 );
}

inline void ltrim(std::string& s) {
    s.erase( 0, s.find_first_not_of(white_spaces) );
}

} // anonymous namespace

namespace cybozu {

void config_parser::load(const std::string& path) {
    m_config.clear();

    std::ifstream is(path);
    if( !is )
        throw std::runtime_error("failed to open " + path);
    unsigned int lineno = 0;

    for( std::string l; std::getline(is, l); ) {
        lineno++;

        ltrim(l);
        rtrim(l);
        if( l.empty() || l[0] == '#' ) continue;

        std::size_t n = l.find('=');
        if( n == std::string::npos )
            throw parse_error(path, lineno);
        std::string key = l.substr(0, n);
        rtrim(key);
        std::string value = l.substr(n+1);
        ltrim(value);

        // unquote the value
        std::size_t len = value.size();
        if( len >= 2 && value[0] == '"' && value[len-1] == '"' ) {
            value = value.substr(1, len-2);
        }

        set(key, value);
    }
}

} // namespace cybozu
