#include <cybozu/logger.hpp>
#include <cybozu/reactor.hpp>
#include <cybozu/signal.hpp>
#include <cybozu/tcp.hpp>
#include <cybozu/util.hpp>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <fcntl.h>
#include <functional>
#include <iostream>
#include <memory>
#include <signal.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

std::string g_prefix;
int g_counter = 0;

const std::size_t MAX_RECV = (2 << 20); // 2 MiB

class filerecv_socket: public cybozu::tcp_socket {
public:
    filerecv_socket(int fd):
        cybozu::tcp_socket(fd),
        m_file( ::creat((g_prefix+std::to_string(++g_counter)).c_str(), 0644) ),
        m_buffer(1 << 20) {
        if( m_fd == -1 )
            cybozu::throw_unix_error(errno, "creat");
    }
    ~filerecv_socket() {
        ::close(m_file);
    }

private:
    const int m_file;
    std::size_t m_written = 0;
    std::vector<char> m_buffer;

    virtual bool on_readable() override final {
        std::size_t received = 0;
        while( true ) {
            ssize_t n = ::recv(m_fd, m_buffer.data(), m_buffer.size(), 0);
            if( n == -1 ) {
                if( errno == EAGAIN || errno == EWOULDBLOCK )
                    break;
                cybozu::throw_unix_error(errno, "recv");
            }
            if( n == 0 ) {
                std::cout << "received and stored "
                          << m_written << " bytes." << std::endl;
                return invalidate();
            }
            received += n;
            m_written += n;
            while( n > 0 ) {
                ssize_t nw = ::write(m_file, m_buffer.data(), n);
                if( nw == -1 )
                    cybozu::throw_unix_error(errno, "write");
                n -= nw;
            }
            if( received > MAX_RECV ) {
                m_reactor->add_readable(*this);
                return true;
            }
        }
        return true;
    }
};

int main(int argc, char** argv) {
    if( argc != 2 ) {
        std::cout << "Usage: filerecv.exe PREFIX" << std::endl;
        return 0;
    }

    g_prefix = argv[1];
    cybozu::logger::set_threshold(cybozu::severity::debug);

    cybozu::reactor r;
    auto sigres = cybozu::signal_setup({SIGHUP, SIGQUIT, SIGTERM, SIGINT});
    sigres->set_handler( [](const struct signalfd_siginfo& si,
                            cybozu::reactor& r) {
                             switch(si.ssi_signo) {
                             case SIGHUP:
                                 std::cerr << "got SIGHUP." << std::endl;
                                 break;
                             case SIGQUIT:
                                 std::cerr << "got SIGQUIT." << std::endl;
                                 break;
                             case SIGTERM:
                                 std::cerr << "got SIGTERM." << std::endl;
                                 break;
                             case SIGINT:
                                 std::cerr << "got SIGINT." << std::endl;
                                 r.quit();
                                 break;
                             }
                         } );
    r.add_resource( std::move(sigres), cybozu::reactor::EVENT_IN );
    cybozu::tcp_server_socket::wrapper w =
        [](int s, const cybozu::ip_address&) {
        return std::unique_ptr<cybozu::tcp_socket>(
            new filerecv_socket(s) ); };
    r.add_resource( cybozu::make_server_socket(NULL, 11111, w),
                    cybozu::reactor::EVENT_IN );
    r.run([](cybozu::reactor& r){
            r.fix_garbage();
            r.gc();});
    return 0;
}
