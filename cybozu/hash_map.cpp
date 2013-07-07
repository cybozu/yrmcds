// (C) 2013 Cybozu.

#include "hash_map.hpp"

#include <cmath>

namespace cybozu {

unsigned int nearest_prime(unsigned int n) noexcept {
    static_assert( sizeof(n) >= 4, "Too small unsigned int." );
    if( n == 2 ) return 2;
    for( unsigned int i = n|1; ; i += 2 ) {
        bool prime = true;
        unsigned int r = (unsigned int)std::rint( std::sqrt((double)i) );
        for( unsigned int j = 3; j <= r; j += 2 ) {
            if( (i % j) == 0 ) {
                prime = false;
                break;
            }
        }
        if( prime ) return i;
    }
}

} // namespace cybozu
