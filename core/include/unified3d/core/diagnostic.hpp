#pragma once

#include <algorithm>
#include <string>
#include <vector>

namespace unified3d {

enum class DiagnosticSeverity {
    info,
    warning,
    error,
};

struct Diagnostic {
    DiagnosticSeverity severity{DiagnosticSeverity::info};
    std::string code;
    std::string message;
    std::string path;

    [[nodiscard]] bool operator==(const Diagnostic&) const = default;
};

struct ValidationResult {
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool valid() const noexcept {
        return std::ranges::none_of(diagnostics, [](const Diagnostic& diagnostic) {
            return diagnostic.severity == DiagnosticSeverity::error;
        });
    }
};

}  // namespace unified3d
