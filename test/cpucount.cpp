#include <thread>
#include <iostream>

int main() {
    std::cout << std::thread::hardware_concurrency() << std::endl;
    return 0;
}
