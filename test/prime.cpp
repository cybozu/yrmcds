#include <cybozu/hash_map.hpp>
#include <cybozu/test.hpp>

#include <map>

const std::map<unsigned int, unsigned int> ANS = {
    {0, 1},
    {1, 1},
    {2, 2},
    {3, 3},
    {4, 5},
    {5, 5},
    {6, 7},
    {7, 7},
    {8, 11},
    {9, 11},
    {10, 11},
    {11, 11},
    {12, 13},
    {13, 13},
    {14, 17},
    {15, 17},
    {16, 17},
    {17, 17},
    {18, 19},
    {19, 19},
    {20, 23},
    {1024, 1031},
    {2097152, 2097169},
    {2147483648, 2147483659}
};

AUTOTEST(prime) {
    for( auto& kv: ANS ) {
        cybozu_assert( cybozu::nearest_prime(kv.first) == kv.second );
    }
}
