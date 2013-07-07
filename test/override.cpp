struct A {
    virtual void f() {}
    virtual ~A() {}
};

struct B: public A {
    void f() override {}
};

int main() {
    B b;
    return 0;
}
