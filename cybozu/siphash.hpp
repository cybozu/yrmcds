// siphash.hpp
// (C) 2013 Cybozu.

#ifndef CYBOZU_SIPHASH_HPP
#define CYBOZU_SIPHASH_HPP

#include <cstddef>
#include <cstdint>

namespace cybozu {

// Seed <siphash24>.
//
// This function presets a 128bit key for <siphash24>.
// Call this once before using <siphash24>.
void siphash24_seed(const char key[16]);

// Calculate 64bit keyed secure hash using SipHash algorithm.
// @src     Pointer to a memory region.
// @src_sz  Region size in bytes.
//
// This function calculates a 64bit hash value using
// [SipHash](https://131002.net/siphash/) algorithm.
//
// @return  64bit hash value.
std::uint64_t siphash24(const void *src, std::size_t src_sz);

} // namespace cybozu

#endif // CYBOZU_SIPHASH_HPP
