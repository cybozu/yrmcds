#include <cybozu/hash_map.hpp>
#include <cybozu/test.hpp>

#include <string>
#include <iostream>

using hash_map = cybozu::hash_map<std::string>;

bool updater(const cybozu::hash_key& k, std::string& s) {
    std::cerr << "Updating " << k.str() << std::endl;
    s.append("+");
    return true;
}

bool expired(const cybozu::hash_key&, std::string&) {
    return false;
}

std::string creator(const cybozu::hash_key& k) {
    return std::string("hoge");
}

const char key1[] = "abc";
const cybozu::hash_key hkey1(key1, sizeof(key1));
const char key2[] = "def";
const cybozu::hash_key hkey2(key2, sizeof(key2));

AUTOTEST(t) {
    hash_map m(8);
    cybozu_assert( m.bucket_count() >= 8 );
    cybozu_assert( m.apply(hkey1, updater, nullptr) == false );
    cybozu_assert( m.apply(hkey1, nullptr, creator) == true );
    cybozu_assert( m.remove(hkey1, nullptr) == true );
    cybozu_assert( m.remove(hkey1, nullptr) == false );
    cybozu_assert( m.apply(hkey1, nullptr, creator) == true );
    cybozu_assert( m.apply(hkey1, nullptr, creator) == false );
    cybozu_assert( m.apply(hkey1, updater, creator) == true );
    cybozu_assert( m.apply(hkey2, nullptr, creator) == true );
    cybozu_assert( m.apply(hkey2, expired, nullptr) == false );
    cybozu_assert( m.apply(hkey2, expired, creator) == false );
    for( hash_map::bucket& b: m ) {
        b.gc([](const cybozu::hash_key& key, std::string& s) {
                return s.size() == 4; });
    }
    cybozu_assert( m.apply(hkey1, updater, nullptr) == true );
    cybozu_assert( m.apply(hkey2, updater, nullptr) == false );
}
