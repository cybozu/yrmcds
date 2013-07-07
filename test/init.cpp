#include <iostream>

struct A {
    A() {
        std::cout << "A::ctor" << std::endl;
    }
    void f() {
        std::cout << "A::f" << std::endl;
    }
};

int main() {
    A a{};
    a.f();
    return 0;
}
