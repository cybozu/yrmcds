#include <algorithm>
#include <cassert>
#include <iostream>
#include <vector>

void foo() {
    std::vector<bool> vb(30);
    //std::fill(vb.begin(), vb.end(), true);
    assert( std::none_of(vb.begin(), vb.end(), [](bool b){return b;}) );
    vb[3] = true;
    assert( ! std::none_of(vb.begin(), vb.end(), [](bool b){return b;}) );
    assert( std::any_of(vb.begin(), vb.end(), [](bool b){return b;}) );
    assert( ! std::all_of(vb.begin(), vb.end(), [](bool b){return b;}) );
    std::fill(vb.begin(), vb.end(), true);
    assert( std::all_of(vb.begin(), vb.end(), [](bool b){return b;}) );
}

int main(int argc, char** argv) {
    for( int i = 0; i < 10; ++i )
        foo();
    return 0;
}
