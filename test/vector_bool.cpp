#include <cybozu/test.hpp>

#include <algorithm>
#include <cassert>
#include <iostream>
#include <vector>

AUTOTEST(vector_bool) {
    std::vector<bool> vb(30);
    cybozu_assert( std::none_of(vb.begin(), vb.end(), [](bool b){return b;}) );
    vb[3] = true;
    cybozu_assert( !std::none_of(vb.begin(), vb.end(), [](bool b){return b;}) );
    cybozu_assert( std::any_of(vb.begin(), vb.end(), [](bool b){return b;}) );
    cybozu_assert( !std::all_of(vb.begin(), vb.end(), [](bool b){return b;}) );
    std::fill(vb.begin(), vb.end(), true);
    cybozu_assert( std::all_of(vb.begin(), vb.end(), [](bool b){return b;}) );
}
