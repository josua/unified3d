#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <unified3d/core/analysis/analysis_record.hpp>
#include <unified3d/core/diagnostic.hpp>

namespace unified3d::operations::analysis {

enum class InputSide {
    a,
    b,
};

enum class ComparisonLevelStatus {
    match,
    different,
    not_comparable,
    not_available,
};

enum class CompatibilityClassification {
    exact_topology_match,
    direct_skin_transfer_compatible,
    geometry_match,
    geometric_vertex_mapping_required,
    spatial_match,
    spatial_skin_transfer_required,
    similar_geometry,
    advanced_transfer_required,
    incompatible,
};

using EvidenceValue = std::variant<std::string, bool, unified3d::analysis::Count, double>;
using Evidence = std::map<std::string, EvidenceValue, std::less<>>;

struct ComparisonLevel {
    std::size_t level{};
    std::string name;
    ComparisonLevelStatus status{ComparisonLevelStatus::not_available};
    std::optional<double> score;
    Evidence evidence;
};

struct RigDonorGeometryTargetPattern {
    bool detected{false};
    std::optional<InputSide> donor;
    std::optional<InputSide> target;
    std::optional<std::string> recommended_next_check;
};

struct CompatibilityResult {
    std::optional<CompatibilityClassification> classification;
    std::optional<double> score;
    double coverage{0.0};
    std::vector<ComparisonLevel> levels;
    std::optional<std::size_t> recommended_next_level;
};

struct AnalysisComparison {
    std::optional<bool> same_mesh_count;
    std::optional<double> triangle_ratio_b_over_a;
    bool index_transfer_ruled_out_by_triangle_count{false};
    RigDonorGeometryTargetPattern rig_donor_geometry_target_pattern;
    bool topology_signatures_comparable{false};
    CompatibilityResult compatibility;
};

struct CompareAnalysisRecordsResult {
    std::optional<AnalysisComparison> comparison;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool success() const noexcept;
};

[[nodiscard]] CompareAnalysisRecordsResult compare_analysis_records(
    const unified3d::analysis::AnalysisRecord& a,
    const unified3d::analysis::AnalysisRecord& b
);

}  // namespace unified3d::operations::analysis
