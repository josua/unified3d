#include <unified3d/runtime/runtime.hpp>

#include <iostream>
#include <string_view>

int main(const int argc, char** argv) {
    unified3d::runtime::Runtime runtime;
    if (argc == 1 || (argc == 2 && std::string_view{argv[1]} == "--stdio")) {
        return unified3d::runtime::run_stdio(runtime, std::cin, std::cout);
    }
    if (std::string_view{argv[1]} == "--pipe") {
        const std::string_view name = argc >= 3
            ? std::string_view{argv[2]}
            : unified3d::runtime::default_windows_pipe_name;
        return unified3d::runtime::run_named_pipe(runtime, name);
    }
    if (argc == 2 && std::string_view{argv[1]} == "--help") {
        std::cout << "Usage: unified3d-runtime [--stdio | --pipe [\\\\.\\pipe\\Name]]\n";
        return 0;
    }
    std::cerr << "Invalid arguments. Use --help.\n";
    return 2;
}
