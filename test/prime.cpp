#include <cybozu/hash_map.hpp>

#include <iostream>

void show_nearest_prime(unsigned int n) {
    std::cout << "nearest_prime(" << n << ") = "
              << cybozu::nearest_prime(n) << std::endl;
}

int main() {
    for( int i = 0; i <= 20; ++i )
        show_nearest_prime(i);
    show_nearest_prime(1024);
    show_nearest_prime(2<<20);
    show_nearest_prime(2<<30);
    return 0;
}
