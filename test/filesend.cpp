#include <cybozu/logger.hpp>
#include <cybozu/reactor.hpp>
#include <cybozu/tcp.hpp>
#include <cybozu/util.hpp>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <fcntl.h>
#include <functional>
#include <iostream>
#include <memory>
#include <sys/stat.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

//const std::size_t CHUNK_SIZE = (2 << 20); // 2 MiB
const std::size_t CHUNK_SIZE = (512 << 10); // 512 KiB

class filesend_socket: public cybozu::tcp_socket {
public:
    filesend_socket(int fd): cybozu::tcp_socket(fd, 2) {}

private:
    virtual bool on_readable() override final { return true; }
};

void filesend(const char* path, cybozu::tcp_socket& s) {
    int fd = open(path, O_RDONLY);
    if( fd == -1 ) return;

    struct stat st;
    if( fstat(fd, &st) == -1 ) {
        std::cerr << "fstat failed." << std::endl;
        close(fd);
        cybozu::throw_unix_error(errno, "fstat");
    }
    off_t length = st.st_size;
    std::cout << "sending " << length << " bytes." << std::endl;

    std::vector<char> buffer(CHUNK_SIZE);
    char* const p_buf = buffer.data();

    while( length ) {
        off_t to_read = std::min(length, (off_t)CHUNK_SIZE);
        ssize_t n = read(fd, p_buf, to_read);
        if( n == -1 ) {
            std::cerr << "read failed." << std::endl;
            close(fd);
            cybozu::throw_unix_error(errno, "read");
        }
        length -= n;

        if( length == 0 ) {
            std::cerr << "closing." << std::endl;
            s.send_close(p_buf, n);
        } else {
            s.send(p_buf, n);
        }
    }

    close(fd);
}

int main(int argc, char** argv) {
    if( argc != 3 ) {
        std::cout << "Usage: filesend.exe FILENAME THREADS" << std::endl;
        return 0;
    }

    cybozu::logger::set_threshold(cybozu::severity::debug);

    const char* path = argv[1];
    int n_threads = std::stoi(argv[2]);
    if( n_threads < 0 ) {
        std::cout << "Invalid number of threads." << std::endl;
        return 1;
    }

    cybozu::reactor r;

    std::vector<std::thread> threads;
    for( int i = 0; i < n_threads; ++i ) {
        int c = cybozu::tcp_connect(NULL, 11111);
        if( c == -1 ) {
            std::cerr << "failed to connect to localhost:11111" << std::endl;
            return 2;
        }
        std::unique_ptr<filesend_socket> s( new filesend_socket(c) );
        threads.emplace_back(filesend, path, std::ref(*s));
        r.add_resource( std::move(s), cybozu::reactor::EVENT_OUT );
    }

    r.run([&threads](cybozu::reactor& r) {
            r.fix_garbage();
            r.gc();
            if( r.size() == 0 ) {
                for( auto& t: threads )
                    t.join();
                r.quit();
            }
        });
    return 0;
}
