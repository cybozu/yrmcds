#include <algorithm>
#include <iostream>
#include <vector>
#include <new>
#include <cstdlib>
#include <cstring>

#if DEBUG_NEW
void* operator new(std::size_t n) {
    std::cerr << "allocating " << n << " bytes." << std::endl;
    return std::malloc(n);
}

void operator delete(void* p) {
    std::cerr << "deallocating" << std::endl;
    return std::free(p);
}
#endif

int main(int argc, char** argv) {
    std::cout << "sizeof(std::vector<char>) = "
              << sizeof(std::vector<char>) << std::endl;
    std::vector<char> v {'a', 'b', 'c', 'd', 'e', 'f'};
    std::cout << "v.size() = " << v.size() << std::endl;

    auto it = v.begin();
    while( it != v.end() ) {
        if( *it == 'b' || *it == 'c' ) {
            it = v.erase(it);
        } else {
            ++it;
        }
    }
    for( char c: v )
        std::cout << c;
    std::cout << std::endl;

    const char* hello = "Hello World";
    std::vector<char> hv(hello+3, hello+7);
    for( char c: hv ) std:: cerr << c;
    std::cerr << std::endl;

    // without v.erase, the vector size will not be changed.
    v.erase(std::remove_if(v.begin(), v.end(),
                           [](char c) { return (c&1) == 1; }),
            v.end());
    for( char c: v )
        std::cout << c;
    std::cout << std::endl;

    std::vector<char> v2;
    v2.reserve(5);
    v2.push_back('1');
    v2.push_back('2');
    std::vector<char> v3 = {'a', 'b', 'c', 'd'};
    v2 = v3;
    std::cout << v2.capacity() << std::endl;
    return 0;
}
