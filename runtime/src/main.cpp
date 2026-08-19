#include <unified3d/runtime/runtime.hpp>

#include <iostream>

int main() {
    unified3d::runtime::Runtime runtime;
    return unified3d::runtime::run_stdio(runtime, std::cin, std::cout);
}
