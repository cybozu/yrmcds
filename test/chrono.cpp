#include <cybozu/util.hpp>

#include <typeinfo>
#include <chrono>
#include <iostream>

typedef std::chrono::microseconds us_t;

template <typename T>
inline us_t::rep to_us(T start, T end) {
    return std::chrono::duration_cast<us_t>(end-start).count();
}

template <typename T>
inline void print_ratio() {
    cybozu::demangler t(typeid(T).name());
    std::cout << t.name() << " ratio = "
              << T::period::num << "/" << T::period::den
              << " second." << std::endl;
}

int main() {
    auto t1 = std::chrono::steady_clock::now();

    print_ratio<std::chrono::system_clock>();
    print_ratio<std::chrono::steady_clock>();
    print_ratio<std::chrono::high_resolution_clock>();

    std::cout << "std::chrono::high_resolution_clock is "
              << (std::chrono::high_resolution_clock::is_steady ? "" : "not ")
              << "steady." << std::endl;

    static_assert(std::chrono::steady_clock::is_steady,
                  "std::chrono::steady_clock is not steady!");

    auto t2 = std::chrono::steady_clock::now();
    std::cout << "t2 - t1 = "
              << to_us(t1, t2) << "us"
              << std::endl;
    return 0;
}
