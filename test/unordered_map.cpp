#include <unordered_map>
#include <string>
#include <iostream>

int main(int argc, char** argv) {
    std::unordered_map<std::string, std::string> m;
    std::cout << "Default max load factor: " << m.max_load_factor() << std::endl;
    std::cout << "Default bucket count: " << m.bucket_count() << std::endl;
    m.max_load_factor(1.0);
    m.reserve(65536);
    std::cout << "Bucket count now: " << m.bucket_count() << std::endl;
    return 0;
}
