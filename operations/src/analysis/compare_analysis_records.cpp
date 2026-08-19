#include <unified3d/operations/analysis/compare_analysis_records.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <string_view>

namespace unified3d::operations::analysis {
namespace {

using CoreAnalysis = unified3d::analysis::AnalysisRecord;
using Count = unified3d::analysis::Count;

std::optional<double> ratio_score(
    const std::optional<Count> a,
    const std::optional<Count> b
) {
    if (!a.has_value() || !b.has_value()) {
        return std::nullopt;
    }
    if (*a == *b) {
        return 1.0;
    }
    if (*a == 0U || *b == 0U) {
        return 0.0;
    }
    const double value_a = static_cast<double>(*a);
    const double value_b = static_cast<double>(*b);
    return std::min(value_a, value_b) / std::max(value_a, value_b);
}

std::optional<double> ratio_score(const double a, const double b) {
    if (!std::isfinite(a) || !std::isfinite(b) || a < 0.0 || b < 0.0) {
        return std::nullopt;
    }
    if (a == b) {
        return 1.0;
    }
    if (a == 0.0 || b == 0.0) {
        return 0.0;
    }
    return std::min(a, b) / std::max(a, b);
}

std::optional<double> bounds_similarity(
    const std::optional<unified3d::analysis::Bounds3d>& a,
    const std::optional<unified3d::analysis::Bounds3d>& b
) {
    if (!a.has_value() || !b.has_value()) {
        return std::nullopt;
    }

    double score_sum = 0.0;
    for (std::size_t axis = 0; axis < a->min.size(); ++axis) {
        const double extent_a = std::abs(a->max[axis] - a->min[axis]);
        const double extent_b = std::abs(b->max[axis] - b->min[axis]);
        const auto axis_score = ratio_score(extent_a, extent_b);
        if (!axis_score.has_value()) {
            return std::nullopt;
        }
        score_sum += *axis_score;
    }
    return score_sum / 3.0;
}

bool has_rig(const CoreAnalysis& record) {
    return record.skin.present == true
        || (record.skeleton.joint_count.has_value() && *record.skeleton.joint_count > 0U);
}

bool explicitly_unrigged(const CoreAnalysis& record) {
    return record.skin.present == false
        && (!record.skeleton.joint_count.has_value() || *record.skeleton.joint_count == 0U);
}

bool denser_than(const CoreAnalysis& candidate, const CoreAnalysis& source) {
    return candidate.geometry.triangle_count.has_value()
        && source.geometry.triangle_count.has_value()
        && *candidate.geometry.triangle_count > *source.geometry.triangle_count;
}

RigDonorGeometryTargetPattern donor_target_pattern(
    const CoreAnalysis& a,
    const CoreAnalysis& b
) {
    if (has_rig(a) && explicitly_unrigged(b) && denser_than(b, a)) {
        return {
            .detected = true,
            .donor = InputSide::a,
            .target = InputSide::b,
            .recommended_next_check = "spatial_alignment",
        };
    }
    if (has_rig(b) && explicitly_unrigged(a) && denser_than(a, b)) {
        return {
            .detected = true,
            .donor = InputSide::b,
            .target = InputSide::a,
            .recommended_next_check = "spatial_alignment",
        };
    }
    return {};
}

struct VertexMeasurement {
    std::optional<Count> count;
    std::optional<unified3d::analysis::GeometricVertexSemantic> geometric_semantic;
    bool render_vertices{false};
};

VertexMeasurement vertex_measurement(const CoreAnalysis& record) {
    if (record.asset.format == unified3d::analysis::AssetFormat::gltf
        && record.geometry.render_vertex_count.has_value()) {
        return {
            .count = record.geometry.render_vertex_count,
            .geometric_semantic = std::nullopt,
            .render_vertices = true,
        };
    }
    return {
        .count = record.geometry.geometric_vertex_count,
        .geometric_semantic = record.geometry.geometric_vertex_semantic,
        .render_vertices = false,
    };
}

bool same_vertex_semantic(const VertexMeasurement& a, const VertexMeasurement& b) {
    if (a.render_vertices || b.render_vertices) {
        return a.render_vertices && b.render_vertices;
    }
    return a.geometric_semantic.has_value() && b.geometric_semantic.has_value()
        && a.geometric_semantic == b.geometric_semantic;
}

bool topology_signatures_comparable(const CoreAnalysis& a, const CoreAnalysis& b) {
    if (!a.geometry.topology_signature.has_value()
        || !b.geometry.topology_signature.has_value()) {
        return false;
    }
    return a.geometry.topology_signature->algorithm
            == b.geometry.topology_signature->algorithm
        && a.geometry.topology_signature->domain
            == b.geometry.topology_signature->domain;
}

void prefix_diagnostics(
    std::vector<Diagnostic>& destination,
    const ValidationResult& validation,
    const std::string_view input
) {
    for (const auto& source : validation.diagnostics) {
        Diagnostic copy = source;
        const std::string suffix = source.path.starts_with("$.")
            ? source.path.substr(2)
            : source.path;
        copy.path = "$.inputs." + std::string{input} + "." + suffix;
        destination.push_back(std::move(copy));
    }
}

CompatibilityResult build_compatibility(
    const CoreAnalysis& a,
    const CoreAnalysis& b,
    const bool donor_detected
) {
    CompatibilityResult result;
    result.levels.reserve(7U);
    result.levels.push_back(
        ComparisonLevel{
            .level = 0U,
            .name = "analysis_records",
            .status = ComparisonLevelStatus::match,
            .score = 1.0,
            .evidence = {{"normalized", true}},
        }
    );

    if (a.asset.coordinate_system.complete() && b.asset.coordinate_system.complete()) {
        const bool same = a.asset.coordinate_system == b.asset.coordinate_system;
        result.levels.push_back(
            ComparisonLevel{
                .level = 1U,
                .name = "coordinate_system",
                .status = same ? ComparisonLevelStatus::match
                               : ComparisonLevelStatus::different,
                .score = same ? 1.0 : 0.0,
                .evidence = {},
            }
        );
    } else {
        result.levels.push_back(
            ComparisonLevel{
                .level = 1U,
                .name = "coordinate_system",
                .status = ComparisonLevelStatus::not_available,
                .score = std::nullopt,
                .evidence = {{"reason", std::string{"axes_or_units_not_computed"}}},
            }
        );
    }

    const auto bounds_score = bounds_similarity(a.geometry.bounds, b.geometry.bounds);
    result.levels.push_back(
        ComparisonLevel{
            .level = 2U,
            .name = "bounds",
            .status = !bounds_score.has_value()
                ? ComparisonLevelStatus::not_available
                : *bounds_score >= 0.99 ? ComparisonLevelStatus::match
                                        : ComparisonLevelStatus::different,
            .score = bounds_score,
            .evidence = bounds_score.has_value()
                ? Evidence{{"extent_similarity", *bounds_score}}
                : Evidence{{"reason", std::string{"bounds_not_computed"}}},
        }
    );

    const auto mesh_score = ratio_score(a.geometry.mesh_count, b.geometry.mesh_count);
    result.levels.push_back(
        ComparisonLevel{
            .level = 3U,
            .name = "mesh_structure",
            .status = !mesh_score.has_value()
                ? ComparisonLevelStatus::not_available
                : *mesh_score == 1.0 ? ComparisonLevelStatus::match
                                     : ComparisonLevelStatus::different,
            .score = mesh_score,
            .evidence = mesh_score.has_value()
                ? Evidence{
                    {"a_mesh_count", *a.geometry.mesh_count},
                    {"b_mesh_count", *b.geometry.mesh_count},
                }
                : Evidence{{"reason", std::string{"mesh_count_not_computed"}}},
        }
    );

    const auto triangle_score = ratio_score(
        a.geometry.triangle_count,
        b.geometry.triangle_count
    );
    result.levels.push_back(
        ComparisonLevel{
            .level = 4U,
            .name = "triangle_statistics",
            .status = !triangle_score.has_value()
                ? ComparisonLevelStatus::not_available
                : *triangle_score == 1.0 ? ComparisonLevelStatus::match
                                         : ComparisonLevelStatus::different,
            .score = triangle_score,
            .evidence = triangle_score.has_value()
                ? Evidence{
                    {"a_triangle_count", *a.geometry.triangle_count},
                    {"b_triangle_count", *b.geometry.triangle_count},
                    {"density_similarity", *triangle_score},
                }
                : Evidence{{"reason", std::string{"triangle_count_not_computed"}}},
        }
    );

    const auto vertex_a = vertex_measurement(a);
    const auto vertex_b = vertex_measurement(b);
    if (!vertex_a.count.has_value() || !vertex_b.count.has_value()) {
        result.levels.push_back(
            ComparisonLevel{
                .level = 5U,
                .name = "vertex_statistics",
                .status = ComparisonLevelStatus::not_available,
                .score = std::nullopt,
                .evidence = {{"reason", std::string{"vertex_count_not_computed"}}},
            }
        );
    } else if (!same_vertex_semantic(vertex_a, vertex_b)) {
        result.levels.push_back(
            ComparisonLevel{
                .level = 5U,
                .name = "vertex_statistics",
                .status = ComparisonLevelStatus::not_comparable,
                .score = std::nullopt,
                .evidence = {{"reason", std::string{"vertex_semantics_differ"}}},
            }
        );
    } else {
        const auto vertex_score = ratio_score(vertex_a.count, vertex_b.count);
        result.levels.push_back(
            ComparisonLevel{
                .level = 5U,
                .name = "vertex_statistics",
                .status = *vertex_score == 1.0 ? ComparisonLevelStatus::match
                                               : ComparisonLevelStatus::different,
                .score = vertex_score,
                .evidence = {
                    {"a_count", *vertex_a.count},
                    {"b_count", *vertex_b.count},
                },
            }
        );
    }

    const auto& signature_a = a.geometry.topology_signature;
    const auto& signature_b = b.geometry.topology_signature;
    const bool comparable = topology_signatures_comparable(a, b);
    if (!signature_a.has_value() || !signature_b.has_value()) {
        result.levels.push_back(
            ComparisonLevel{
                .level = 6U,
                .name = "topology_signature",
                .status = ComparisonLevelStatus::not_available,
                .score = std::nullopt,
                .evidence = {{"reason", std::string{"signature_not_computed"}}},
            }
        );
    } else if (!comparable) {
        result.levels.push_back(
            ComparisonLevel{
                .level = 6U,
                .name = "topology_signature",
                .status = ComparisonLevelStatus::not_comparable,
                .score = std::nullopt,
                .evidence = {{"reason", std::string{"signature_algorithms_or_domains_differ"}}},
            }
        );
    } else {
        const bool same = signature_a->digest == signature_b->digest;
        result.levels.push_back(
            ComparisonLevel{
                .level = 6U,
                .name = "topology_signature",
                .status = same ? ComparisonLevelStatus::match
                               : ComparisonLevelStatus::different,
                .score = same ? 1.0 : 0.0,
                .evidence = {{"same_digest", same}},
            }
        );
    }

    std::vector<double> measured_scores;
    measured_scores.reserve(6U);
    for (std::size_t index = 1U; index < result.levels.size(); ++index) {
        if (result.levels[index].score.has_value()) {
            measured_scores.push_back(*result.levels[index].score);
        }
    }
    result.coverage = static_cast<double>(measured_scores.size()) / 6.0;
    if (!measured_scores.empty()) {
        result.score = std::accumulate(measured_scores.begin(), measured_scores.end(), 0.0)
            / static_cast<double>(measured_scores.size());
    }

    if (result.levels[6U].status == ComparisonLevelStatus::match) {
        result.classification = CompatibilityClassification::exact_topology_match;
    } else if (donor_detected && triangle_score.has_value() && *triangle_score < 1.0) {
        result.classification = CompatibilityClassification::advanced_transfer_required;
    }

    const auto unresolved = std::ranges::find_if(
        result.levels,
        [](const ComparisonLevel& level) {
            return level.status == ComparisonLevelStatus::not_available
                || level.status == ComparisonLevelStatus::not_comparable;
        }
    );
    result.recommended_next_level = unresolved == result.levels.end()
        ? std::optional<std::size_t>{7U}
        : std::optional<std::size_t>{unresolved->level};
    return result;
}

}  // namespace

bool CompareAnalysisRecordsResult::success() const noexcept {
    return comparison.has_value()
        && std::ranges::none_of(diagnostics, [](const Diagnostic& diagnostic) {
            return diagnostic.severity == DiagnosticSeverity::error;
        });
}

CompareAnalysisRecordsResult compare_analysis_records(
    const CoreAnalysis& a,
    const CoreAnalysis& b
) {
    CompareAnalysisRecordsResult result;
    const ValidationResult validation_a = validate_analysis_record(a);
    const ValidationResult validation_b = validate_analysis_record(b);
    prefix_diagnostics(result.diagnostics, validation_a, "a");
    prefix_diagnostics(result.diagnostics, validation_b, "b");
    if (!validation_a.valid() || !validation_b.valid()) {
        return result;
    }

    AnalysisComparison comparison;
    if (a.geometry.mesh_count.has_value() && b.geometry.mesh_count.has_value()) {
        comparison.same_mesh_count = a.geometry.mesh_count == b.geometry.mesh_count;
    }
    if (a.geometry.triangle_count.has_value()
        && b.geometry.triangle_count.has_value()
        && *a.geometry.triangle_count > 0U) {
        comparison.triangle_ratio_b_over_a =
            static_cast<double>(*b.geometry.triangle_count)
            / static_cast<double>(*a.geometry.triangle_count);
    }
    comparison.index_transfer_ruled_out_by_triangle_count =
        a.geometry.triangle_count.has_value()
        && b.geometry.triangle_count.has_value()
        && a.geometry.triangle_count != b.geometry.triangle_count;
    comparison.rig_donor_geometry_target_pattern = donor_target_pattern(a, b);
    comparison.topology_signatures_comparable = topology_signatures_comparable(a, b);
    comparison.compatibility = build_compatibility(
        a,
        b,
        comparison.rig_donor_geometry_target_pattern.detected
    );
    result.comparison = std::move(comparison);
    return result;
}

}  // namespace unified3d::operations::analysis
