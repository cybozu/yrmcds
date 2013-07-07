#include <system_error>
#include <cerrno>
#include <sys/types.h>
#include <fcntl.h>
#include <exception>
#include <iostream>

int main(int argc, char** argv) {
    open("/hoge/fuga/faaa", O_WRONLY|O_CREAT, 0700);
    try {
        throw std::system_error(errno, std::system_category(), "open");
    } catch( const std::exception& e ) {
        std::cout << e.what() << std::endl;
    }

    auto ec = std::system_category().default_error_condition(3);
    std::cout << "value=" << ec.value() << ", message=" << ec.message()
              << std::endl;
    return 0;
}
