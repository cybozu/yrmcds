#include <thread>
#include <iostream>

void f() {
    char c;
    std::cin >> c;
}

int main() {
    std::thread t(f);
    t.detach();
    std::cout << "detached" << std::endl;
    return 0;
}
