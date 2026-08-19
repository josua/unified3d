#pragma once

#include <optional>
#include <vector>

#include <nlohmann/json.hpp>

#include <unified3d/core/analysis/analysis_record.hpp>
#include <unified3d/core/diagnostic.hpp>
#include <unified3d/operations/analysis/compare_analysis_records.hpp>

namespace unified3d::runtime::json_codec {

using Json = nlohmann::json;

struct DecodeAnalysisResult {
    std::optional<unified3d::analysis::AnalysisRecord> record;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool valid() const noexcept;
};

[[nodiscard]] DecodeAnalysisResult decode_analysis_record(const Json& value);
[[nodiscard]] Json encode_diagnostic(const Diagnostic& diagnostic);
[[nodiscard]] Json encode_diagnostics(const std::vector<Diagnostic>& diagnostics);
[[nodiscard]] Json encode_analysis_comparison(
    const unified3d::operations::analysis::AnalysisComparison& comparison,
    const Json& input_a,
    const Json& input_b
);

}  // namespace unified3d::runtime::json_codec
