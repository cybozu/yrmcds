#include <cybozu/reactor.hpp>
#include <cybozu/util.hpp>

#include <cassert>
#include <cerrno>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <unistd.h>

int g_counter = 0;
bool got_hangup = false;
bool data_read = false;

class in_resource: public cybozu::resource {
public:
    in_resource(int fd): cybozu::resource(fd) {}
    ~in_resource() {
        std::cout << "stdin_resource dtor." << std::endl;
    }

private:
    virtual bool on_readable() override final {
        char buf[512];
        buf[sizeof(buf) - 1] = '\0';
        ssize_t t = ::read(fileno(), buf, sizeof(buf) - 1);
        if( t == -1 )
            cybozu::throw_unix_error(errno, "read");
        std::cout << "aaa: " << t << std::endl;
        buf[t] = '\0';
        std::cout << "read: " << buf << std::endl;
        data_read = true;
        return false;
    }
    virtual bool on_writable() override final {
        return true;
    }
    virtual bool on_hangup() override final {
        std::cout << "got hangup!" << std::endl;
        got_hangup = true;
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

    int fds[2];
    if( ::pipe2(fds, O_NONBLOCK) == -1 )
        cybozu::throw_unix_error(errno, "pipe");
    if( ::write(fds[1], "Hello!", 6) == -1 )
        cybozu::throw_unix_error(errno, "write");
    ::close(fds[1]);
    cybozu::reactor r;
    std::unique_ptr<cybozu::resource> res(new in_resource(fds[0]));
    r.add_resource(std::move(res), cybozu::reactor::EVENT_IN);
    r.run(f);

    assert(got_hangup);
    assert(data_read);
    return 0;
}
