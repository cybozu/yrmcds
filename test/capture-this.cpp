#include <iostream>

struct A {
    void foo() { std::cout << "Hello" << std::endl; }

    void bar() {
        [this]() { foo(); } ();
    }
};

int main() {
    A a;
    a.bar();
    return 0;
}
