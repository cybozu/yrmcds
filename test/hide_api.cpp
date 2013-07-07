#include <iostream>

class A {
public:
    A() {}
    virtual ~A() {}
    virtual int f() = 0;
};

class B: public A {
public:
    B(): A() {}
    virtual int g() { return 100; }
private:
    virtual int f() { return 3; }
};

int main() {
    A* a = new B;
    std::cout << a->f() << std::endl;
    delete a;

    // This causes compilation error, as expected!
    //
    //B b;
    //std::cout << b->f() << std::endl;
}
