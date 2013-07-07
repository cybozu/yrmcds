#include <cybozu/reactor.hpp>
#include <cybozu/tcp.hpp>
#include <cybozu/logger.hpp>

#include <vector>
#include <cstddef>
#include <sys/types.h>
#include <sys/socket.h>
#include <cerrno>
#include <iostream>
#include <system_error>
#include <thread>

class echo_socket: public cybozu::tcp_socket {
public:
    echo_socket(int fd): tcp_socket(fd, 1), m_buffer(1 <<20) {}

    static bool on_accept(int s) {
        std::cerr << "accepted." << std::endl;
        return true;
    }

private:
    std::vector<char> m_buffer;

    void echo_back(std::size_t len) {
        send(m_buffer.data(), len, false);
        send(m_buffer.data(), len, true);
    }

    virtual bool on_readable() override final {
        std::size_t received = 0;
        while( true ) {
            if( m_buffer.size() == received ) {
                std::cerr << "Too large data received." << std::endl;
                received = 0;
            }
            ssize_t n = ::recv(m_fd, &m_buffer[received],
                               m_buffer.size() - received, 0);
            if( n == -1 ) {
                if( errno == EINTR ) continue;
                if( errno == EAGAIN || errno == EWOULDBLOCK ) {
                    echo_back(received);
                    return true;
                }
                auto ecnd = std::system_category().default_error_condition(errno);
                cybozu::logger::error() << "<echo_socket::on_readable>: ("
                                        << ecnd.value() << ") "
                                        << ecnd.message();
                return false;
            }
            if( n == 0 ) return invalidate();
            received += n;
        }
    }
};

int main(int argc, char** argv) {
    if( argc == 1 ) {
        std::cout << "Usage: echo_server.exe PORT" << std::endl;
        return 0;
    }
    uint16_t port = static_cast<uint16_t>(std::stoi(argv[1]));
    cybozu::reactor r;
    cybozu::tcp_server_socket::wrapper w =
        [](int s, const cybozu::ip_address&) {
        return std::unique_ptr<cybozu::tcp_socket>(
            new echo_socket(s) ); };
    r.add_resource( cybozu::make_server_socket(NULL, port, w),
                    cybozu::reactor::EVENT_IN );
    r.run([](cybozu::reactor& r){ std::cerr << "."; });
    return 0;
}
