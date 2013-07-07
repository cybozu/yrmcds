#include <iterator>
#include <iostream>

int main() {
    const int a[] = {1, 2, 3, 4, 5};
    for( auto i: a ) {
        std::cout << i << std::endl;
    }
    return 0;
}
