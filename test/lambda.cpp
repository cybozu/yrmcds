#include <iostream>

int main(int argc, char** argv) {
    int x = 3;
    auto foo = [=]() mutable { ++x; return x * x; };
    std::cout << foo() << std::endl;
    std::cout << foo() << std::endl;
    return 0;
}
