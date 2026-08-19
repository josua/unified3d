#pragma once

#include <iosfwd>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace unified3d::runtime {

class Runtime final {
public:
    Runtime();
    ~Runtime();

    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
    Runtime(Runtime&&) noexcept;
    Runtime& operator=(Runtime&&) noexcept;

    [[nodiscard]] std::optional<std::string> handle_message(std::string_view message);
    [[nodiscard]] bool shutdown_requested() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

int run_stdio(Runtime& runtime, std::istream& input, std::ostream& output);

}  // namespace unified3d::runtime
