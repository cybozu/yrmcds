// move and copy test

#include <iostream>
#include <vector>
#include <memory>
#include <unordered_map>

struct A {
    A() { std::cout << "A ctor." << std::endl; }
    A(A&& rhs) noexcept { std::cout << "A move." << std::endl; moved = true; }
    A(const A&) = delete;
    ~A() { if( ! moved ) std::cout << "A dtor." << std::endl; }

private:
    bool moved = false;
};

std::vector<A> v;

void f(A a) {
    v.push_back( std::move(a) );
}

void g() {
    std::unique_ptr<A> pa( new A );
    typedef std::unordered_map<int, std::unique_ptr<A> > map_type;
    map_type m;
    m.emplace( 3, std::move(pa) );
    std::cout << "get A." << std::endl;
    auto it = m.find(3);
    it->second.get();
    std::cout << "got A." << std::endl;

    std::cout << "get A take2." << std::endl;
    std::unique_ptr<A>& alias = m.at(3);
    A a( std::move(*alias) );
    std::cout << "got A take2." << std::endl;
}

int main(int argc, char** argv) {
    A a;
    f( std::move(a) );

    std::vector<int> iv = {1, 2, 3};
    std::vector<int> iv2 = iv;
    for( auto i : iv2 ) {
        std::cout << i << std::endl;
    }
    g();
    return 0;
}
