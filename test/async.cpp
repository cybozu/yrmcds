#include <string>
#include <future>
#include <iostream>

std::string copy_string(const std::string& s) {
    return s;
}

int find_answer() {
    throw std::runtime_error("hoge!");
    return 1;
}

int main() {
    std::string s("hello");
    //auto f = std::async(std::launch::async, copy_string, std::cref(s));
    //auto f = std::async(std::launch::async, copy_string, s);
    auto f = std::async(std::launch::async, [&]() {
            return copy_string(s);
        });
    s = "goodbye";
    std::cout << f.get() << " world!\n";

    auto g = std::async(std::launch::async, find_answer);
    try {
        g.get();
    } catch(const std::runtime_error& e) {
        std::cerr << "error: " << e.what() << std::endl;
    }
    return 0;
}
