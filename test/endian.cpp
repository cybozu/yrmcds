#include <cybozu/util.hpp>

#include <cstdint>
#include <iostream>

int main(int argc, char** argv) {
#if __BYTE_ORDER == __LITTLE_ENDIAN
    std::cout << "little endian" << std::endl;
#elif __BYTE_ORDER == __BIG_ENDIAN
    std::cout << "big endian" << std::endl;
#endif
    std::uint64_t n = 0x1234567887654321ULL;
    std::cout << std::hex << "n = 0x" << n << std::endl;
    std::cout << "htobe64(n) = 0x" << htobe64(n) << std::endl;

    std::uint64_t t;
    cybozu::hton(n, (char*)&t);
    std::cout << "hton(n, &t) = 0x" << t << std::endl;

    std::uint64_t n2;
    cybozu::ntoh((char*)&t, n2);
    std::cout << "ntoh(t, n2) = 0x" << n2 << std::endl;
    return 0;
}
