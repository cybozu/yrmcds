#include <functional>
#include <iostream>

int main() {
    int *p = nullptr;
    std::reference_wrapper<int> a[3] = {*p, *p, *p};
    int i = 3;
    a[0] = std::ref(i);
    if( &(a[2].get()) == nullptr ) {
        std::cout << "null!" << std::endl;
    } else {
        std::cout << "wtf" << std::endl;
    }
    a[0].get() = 5;
    std::cout << "i = " << i << std::endl;
    return 0;
}
