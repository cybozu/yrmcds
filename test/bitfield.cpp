#include <iostream>

int main(int argc, char** argv) {
    struct {
        unsigned int n : 1;
    } t;
    t.n = 0;

    for( int i = 0; i < 10; ++i ) {
        std::cout << t.n++ << std::endl;
    }
    return 0;
}
