#include <chrono>
#include <thread>
#include <iostream>

int main() {
    std::cerr << "Hello ";
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    std::cerr << "World!" << std::endl;
    return 0;
}
