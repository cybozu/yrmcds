#include <functional>
#include <iostream>
#include <vector>

struct foo {
    int i;
};

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

    foo f;
    f.i = 3;

    std::vector<std::reference_wrapper<const foo>> v;
    v.emplace_back(f);
    std::cout << "f.i = " << v[0].get().i << std::endl;
    f.i = 5;
    std::cout << "f.i = " << v[0].get().i << std::endl;
    return 0;
}
