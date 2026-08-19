#include <unified3d/runtime/runtime.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#if defined(_WIN32)
#include <windows.h>
#include <sddl.h>
#endif

namespace unified3d::runtime {

#if defined(_WIN32)
namespace {

class HandleOwner final {
public:
    explicit HandleOwner(HANDLE value = INVALID_HANDLE_VALUE) : value_(value) {}
    ~HandleOwner() {
        if (value_ != INVALID_HANDLE_VALUE && value_ != nullptr) {
            CloseHandle(value_);
        }
    }

    HandleOwner(const HandleOwner&) = delete;
    HandleOwner& operator=(const HandleOwner&) = delete;

    [[nodiscard]] HANDLE get() const noexcept { return value_; }

private:
    HANDLE value_;
};

class LocalMemoryOwner final {
public:
    explicit LocalMemoryOwner(void* value = nullptr) : value_(value) {}
    ~LocalMemoryOwner() {
        if (value_ != nullptr) {
            LocalFree(value_);
        }
    }

    LocalMemoryOwner(const LocalMemoryOwner&) = delete;
    LocalMemoryOwner& operator=(const LocalMemoryOwner&) = delete;

private:
    void* value_;
};

bool write_all(const HANDLE pipe, const std::string_view value) {
    std::size_t offset = 0U;
    while (offset < value.size()) {
        const std::size_t remaining = value.size() - offset;
        const DWORD chunk = static_cast<DWORD>(
            std::min<std::size_t>(remaining, static_cast<std::size_t>(MAXDWORD))
        );
        DWORD written = 0U;
        if (WriteFile(pipe, value.data() + offset, chunk, &written, nullptr) == 0
            || written == 0U) {
            return false;
        }
        offset += static_cast<std::size_t>(written);
    }
    return true;
}

bool dispatch_complete_lines(Runtime& runtime, const HANDLE pipe, std::string& pending) {
    std::size_t newline = pending.find('\n');
    while (newline != std::string::npos) {
        std::string line = pending.substr(0U, newline);
        pending.erase(0U, newline + 1U);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty()) {
            const std::optional<std::string> response = runtime.handle_message(line);
            if (response.has_value()) {
                const std::string framed = *response + "\n";
                if (!write_all(pipe, framed)) {
                    return false;
                }
                FlushFileBuffers(pipe);
            }
        }
        if (runtime.shutdown_requested()) {
            return true;
        }
        newline = pending.find('\n');
    }
    return true;
}

}  // namespace
#endif

bool named_pipe_supported() noexcept {
#if defined(_WIN32)
    return true;
#else
    return false;
#endif
}

int run_named_pipe(Runtime& runtime, const std::string_view pipe_name) {
#if defined(_WIN32)
    if (!pipe_name.starts_with(R"(\\.\pipe\)") || pipe_name.size() > 256U
        || pipe_name.size() <= std::string_view{R"(\\.\pipe\)"}.size()) {
        return 2;
    }

    PSECURITY_DESCRIPTOR descriptor = nullptr;
    constexpr const wchar_t* security =
        L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;OW)";
    if (ConvertStringSecurityDescriptorToSecurityDescriptorW(
            security,
            SDDL_REVISION_1,
            &descriptor,
            nullptr
        ) == 0) {
        return 3;
    }
    LocalMemoryOwner descriptor_owner{descriptor};
    SECURITY_ATTRIBUTES attributes{
        .nLength = sizeof(SECURITY_ATTRIBUTES),
        .lpSecurityDescriptor = descriptor,
        .bInheritHandle = FALSE,
    };

    while (!runtime.shutdown_requested()) {
        const HandleOwner pipe{
            CreateNamedPipeA(
                std::string{pipe_name}.c_str(),
                PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                1U,
                64U * 1024U,
                64U * 1024U,
                0U,
                &attributes
            )
        };
        if (pipe.get() == INVALID_HANDLE_VALUE) {
            return 4;
        }

        const BOOL connected = ConnectNamedPipe(pipe.get(), nullptr);
        if (connected == 0 && GetLastError() != ERROR_PIPE_CONNECTED) {
            return 5;
        }

        std::string pending;
        std::array<char, 64U * 1024U> buffer{};
        while (!runtime.shutdown_requested()) {
            DWORD received = 0U;
            const BOOL read = ReadFile(
                pipe.get(),
                buffer.data(),
                static_cast<DWORD>(buffer.size()),
                &received,
                nullptr
            );
            if (read == 0 || received == 0U) {
                const DWORD code = GetLastError();
                if (code == ERROR_BROKEN_PIPE || code == ERROR_NO_DATA || received == 0U) {
                    break;
                }
                return 6;
            }
            pending.append(buffer.data(), static_cast<std::size_t>(received));
            if (!dispatch_complete_lines(runtime, pipe.get(), pending)) {
                break;
            }
        }
        FlushFileBuffers(pipe.get());
        DisconnectNamedPipe(pipe.get());
    }
    return 0;
#else
    static_cast<void>(runtime);
    static_cast<void>(pipe_name);
    return 2;
#endif
}

}  // namespace unified3d::runtime
