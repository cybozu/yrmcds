#include <tuple>
#include <iostream>
#include <vector>
#include <cstdlib>

int main(int argc, char** argv) {
    std::vector<std::tuple<void*, int>> p;
    void* vv = std::malloc(10);
    std::cout << vv << std::endl;
    p.emplace_back(vv, 3);
    for( auto t: p ) {
        void* v;
        std::tie(v, std::ignore) = t;
        std::cout << v << std::endl;
    }
    std::free(vv);

    auto tt = std::make_tuple(1, "abc");
    std::get<0>(tt) = 3;
    int n;
    std::tie(n, std::ignore) = tt;
    std::cout << n << std::endl;
    return 0;
}
