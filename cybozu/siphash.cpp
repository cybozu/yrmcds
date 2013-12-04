/* <MIT License>
 Copyright (c) 2013  Marek Majkowski <marek@popcount.org>

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
 </MIT License>

 Original location:
    https://github.com/majek/csiphash/

 Solution inspired by code from:
    Samuel Neves (supercop/crypto_auth/siphash24/little)
    djb (supercop/crypto_auth/siphash24/little2)
    Jean-Philippe Aumasson (https://131002.net/siphash/siphash24.c)
*/

// Cosmetic changes for C++ on Linux by @ymmt2005

#include "siphash.hpp"
#include <endian.h>

#define ROTATE(x, b) (std::uint64_t)( ((x) << (b)) | ( (x) >> (64 - (b))) )

#define HALF_ROUND(a,b,c,d,s,t)                 \
    a += b; c += d;                             \
    b = ROTATE(b, s) ^ a;                       \
    d = ROTATE(d, t) ^ c;                       \
    a = ROTATE(a, 32);

#define DOUBLE_ROUND(v0,v1,v2,v3)               \
    HALF_ROUND(v0,v1,v2,v3,13,16);              \
    HALF_ROUND(v2,v1,v0,v3,17,21);              \
    HALF_ROUND(v0,v1,v2,v3,13,16);              \
    HALF_ROUND(v2,v1,v0,v3,17,21);

using std::uint8_t;
using std::uint32_t;
using std::uint64_t;

namespace {

uint64_t static_k0;
uint64_t static_k1;

}

namespace cybozu {

void siphash24_seed(const char key[16]) {
    const uint64_t *_key = (uint64_t *)key;
    static_k0 = le64toh(_key[0]);
    static_k1 = le64toh(_key[1]);
}

uint64_t siphash24(const void *src, std::size_t src_sz) {
    uint64_t b = (uint64_t)src_sz << 56;
    const uint64_t *in = (uint64_t*)src;

    uint64_t v0 = static_k0 ^ 0x736f6d6570736575ULL;
    uint64_t v1 = static_k1 ^ 0x646f72616e646f6dULL;
    uint64_t v2 = static_k0 ^ 0x6c7967656e657261ULL;
    uint64_t v3 = static_k1 ^ 0x7465646279746573ULL;

    while (src_sz >= 8) {
        uint64_t mi = le64toh(*in);
        in += 1; src_sz -= 8;
        v3 ^= mi;
        DOUBLE_ROUND(v0,v1,v2,v3);
        v0 ^= mi;
    }

    uint64_t t = 0; uint8_t *pt = (uint8_t *)&t; uint8_t *m = (uint8_t *)in;
    switch (src_sz) {
    case 7: pt[6] = m[6];
    case 6: pt[5] = m[5];
    case 5: pt[4] = m[4];
    case 4: *((uint32_t*)&pt[0]) = *((uint32_t*)&m[0]); break;
    case 3: pt[2] = m[2];
    case 2: pt[1] = m[1];
    case 1: pt[0] = m[0];
    }
    b |= le64toh(t);

    v3 ^= b;
    DOUBLE_ROUND(v0,v1,v2,v3);
    v0 ^= b; v2 ^= 0xff;
    DOUBLE_ROUND(v0,v1,v2,v3);
    DOUBLE_ROUND(v0,v1,v2,v3);
    return (v0 ^ v1) ^ (v2 ^ v3);
}

} // namespace cybozu
