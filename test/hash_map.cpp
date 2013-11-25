#include <cybozu/hash_map.hpp>

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

bool reader(const cybozu::hash_key&, std::string& s) {
    std::cout << s << std::endl;
    return true;
}

std::string creator(const cybozu::hash_key& k) {
    return std::string("hoge");
}

const char key1[] = "abc";
const cybozu::hash_key hkey1(key1, sizeof(key1));
const char key2[] = "def";
const cybozu::hash_key hkey2(key2, sizeof(key2));

int main(int argc, char** argv) {
    hash_map m(8);
    std::cout << "bucket count = " << m.bucket_count() << std::endl;
    std::cerr << std::boolalpha;
    std::cerr << m.apply(hkey1, updater, nullptr) << std::endl;
    std::cerr << m.apply(hkey1, nullptr, creator) << std::endl;
    std::cerr << m.remove(hkey1, nullptr) << std::endl;
    std::cerr << m.remove(hkey1, nullptr) << std::endl;
    std::cerr << m.apply(hkey1, nullptr, creator) << std::endl;
    std::cerr << m.apply(hkey1, nullptr, creator) << std::endl;
    std::cerr << m.apply(hkey1, updater, creator) << std::endl;
    std::cerr << m.apply(hkey1, reader, nullptr) << std::endl;
    std::cerr << m.apply(hkey2, nullptr, creator) << std::endl;
    std::cerr << m.apply(hkey2, expired, nullptr) << std::endl;
    std::cerr << m.apply(hkey2, expired, creator) << std::endl;
    for( hash_map::bucket& b: m ) {
        b.gc([](const cybozu::hash_key& key, std::string& s) {
                std::cout << "collecting " << s << std::endl;
                return s.size() == 4; });
    }
    std::cerr << m.apply(hkey1, reader, nullptr) << std::endl;
    std::cerr << m.apply(hkey2, reader, nullptr) << std::endl;
    return 0;
}
