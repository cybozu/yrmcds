#include <cybozu/reactor.hpp>

#include <iostream>
#include <stdio.h>
#include <string>

int g_counter = 0;

class stdin_resource: public cybozu::resource {
public:
    stdin_resource(): cybozu::resource(STDIN_FILENO) {}
    ~stdin_resource() {
        std::cout << "stdin_resource dtor." << std::endl;
    }

private:
    virtual bool on_readable() override final {
        std::string s;
        std::getline(std::cin, s);
        std::cout << "read: " << s << std::endl;
        return false;
    }
    virtual bool on_writable() override final {
        return true;
    }
};

void f(cybozu::reactor& r) {
    g_counter++;
    if( g_counter > 2 ) {
        r.quit();
        return;
    }
    std::cout << g_counter << std::endl;
    if( r.fix_garbage() )
        r.gc();
}

int main(int argc, char** argv) {
    cybozu::logger::set_threshold(cybozu::severity::debug);
    cybozu::reactor r;
    std::unique_ptr<cybozu::resource> res(new stdin_resource);
    r.add_resource(std::move(res), cybozu::reactor::EVENT_IN);
    r.run(f);
    return 0;
}
