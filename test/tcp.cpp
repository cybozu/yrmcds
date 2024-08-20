#include <cybozu/reactor.hpp>
#include <cybozu/signal.hpp>
#include <cybozu/tcp.hpp>
#include <cybozu/test.hpp>

#include <arpa/inet.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <signal.h>

#include <atomic>
#include <cstdlib>
#include <memory>
#include <thread>

AUTOTEST(fd_exhausted) {
    pid_t pid = ::fork();
    if( pid == 0 ) { 
        // server process

        struct rlimit limit = {100, 100};
        int e = ::setrlimit(RLIMIT_NOFILE, &limit);
        if( e < 0 ) {
            std::cout << "e = " << e << ", err = '" << strerror(errno) << "'\n";
            cybozu_fail("setrlimit failed");
            ::exit(1);
        }

        cybozu::reactor r;

        auto sh = cybozu::signal_setup({SIGTERM});
        sh->set_handler([](const struct signalfd_siginfo& si, cybozu::reactor& r) {
            std::cerr << "got signal (signo=" << si.ssi_signo << ").";
            r.quit();
        });
        r.add_resource(std::move(sh), cybozu::reactor::EVENT_IN);

        struct dummy_socket : public cybozu::tcp_socket {
            dummy_socket(int s): cybozu::tcp_socket(s) {}
            virtual bool on_readable(int) override { return true; }
        };
        auto on_accept = [](int s, const cybozu::ip_address addr) {
            return std::unique_ptr<cybozu::tcp_socket>(new dummy_socket(s));
        };
        r.add_resource(
            std::unique_ptr<cybozu::resource>(
                new cybozu::tcp_server_socket(nullptr, 11214, on_accept, false)
            ),
            cybozu::reactor::EVENT_IN);

        r.run([](cybozu::reactor& r){});
    } else {
        // client process

        ::sleep(1); // wait for server to start
        for( int i = 0; i < 200; ++i ) {
            int sockfd = ::socket(AF_INET, SOCK_STREAM, 0);
            if( sockfd < 0 ) {
                std::cout << "sockfd = " << sockfd << ", err = '" << strerror(errno) << "'\n";
                cybozu_fail("failed to create client socket");
                ::kill(pid, SIGKILL);
                ::exit(1);
            }
            cybozu_assert( sockfd >= 0 );
            struct sockaddr_in addr = {};
            addr.sin_family = AF_INET;
            ::inet_aton("127.0.0.1", &addr.sin_addr);
            addr.sin_port = ::htons(11214);
            int e = ::connect(sockfd, (struct sockaddr*)&addr, sizeof(addr));
            if( e < 0 ) {
                std::cout << "i = " << i << ", e = " << e << ", err = '" << strerror(errno) << "'\n";
                cybozu_fail("failed to connect");
                ::kill(pid, SIGKILL);
                ::exit(1);
            }
        }
        ::sleep(3);
        // Cannot handle this signal if the reactor thread is in a busy loop.
        ::kill(pid, SIGTERM);
    }
}
