// #include "../lz4/lz4.h"

#include <cstddef>
#include <vector>
#include <iostream>
#include <cstdlib>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cstring>

const std::size_t S = 1 << 20;

void fill_seq(std::vector<char>& buf) {
    unsigned char c = 0;
    char* p = buf.data();
    for( std::size_t i = 0; i < S; ++i ) {
        *p = c++;
    }
}

void fill_random(std::vector<char>& buf) {
    int fd = open("/dev/urandom", O_RDONLY);
    if( fd == -1 ) {
        std::cerr << "failed to open /dev/urandom" << std::endl;
        std::exit(1);
    }
    std::size_t to_read = S;
    char* p = buf.data();
    while( to_read != 0 ) {
        ssize_t n = read(fd, p, to_read);
        if( n == -1 ) {
            std::cerr << "failed to read from /dev/urandom" << std::endl;
            std::exit(1);
        }
        p += n;
        to_read -= n;
    }
    close(fd);
}

int main(int argc, char** argv) {
    // std::vector<char> buf1(S);
    // fill_seq(buf1);
    // //fill_random(buf1);
    // std::vector<char> buf2(S);
    // std::vector<char> cbuf(LZ4_COMPRESSBOUND(S));
    // std::cout << "cbuf size = " << cbuf.size() << std::endl;
    // int csize = LZ4_compress(buf1.data(), cbuf.data(), S);
    // if( csize == 0 ) {
    //     std::cerr << "compression failed." << std::endl;
    //     return 1;
    // }
    // std::cout << "compressed: " << csize << " bytes." << std::endl;

    // // beware that the third parameter is the *uncompressed* size.
    // int csize2 = LZ4_decompress_fast(cbuf.data(), buf2.data(), S);
    // if( csize != csize2 ) {
    //     std::cerr << "wtf!?" << std::endl;
    //     return 1;
    // }

    // if( std::memcmp(buf1.data(), buf2.data(), S) != 0 ) {
    //     std::cerr << "verify failed." << std::endl;
    //     return 1;
    // }
    // std::cout << "verify ok." << std::endl;
    return 0;
}
