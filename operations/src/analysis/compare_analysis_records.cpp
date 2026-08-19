#include <unified3d/operations/analysis/compare_analysis_records.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <string_view>

namespace unified3d::operations::analysis {
namespace {

using CoreAnalysis = unified3d::analysis::AnalysisRecord;
using Count = unified3d::analysis::Count;
using Vec3 = std::array<double, 3>;

std::optional<double> ratio_score(double a, double b);

Vec3 axis_vector(const unified3d::analysis::Axis axis) {
    using Axis = unified3d::analysis::Axis;
    switch (axis) {
        case Axis::positive_x: return {1.0, 0.0, 0.0};
        case Axis::negative_x: return {-1.0, 0.0, 0.0};
        case Axis::positive_y: return {0.0, 1.0, 0.0};
        case Axis::negative_y: return {0.0, -1.0, 0.0};
        case Axis::positive_z: return {0.0, 0.0, 1.0};
        case Axis::negative_z: return {0.0, 0.0, -1.0};
    }
    return {};
}

double dot(const Vec3& a, const Vec3& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

Vec3 cross(const Vec3& a, const Vec3& b) {
    return {
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    };
}

struct CanonicalBounds {
    unified3d::analysis::Bounds3d bounds;
    Vec3 extents{};
    Vec3 center{};
    double diagonal{};
};

const std::optional<unified3d::analysis::Bounds3d>& preferred_bounds(
    const CoreAnalysis& record
) {
    return record.geometry.bounds.has_value()
        ? record.geometry.bounds
        : record.scene.bounds;
}

std::optional<CanonicalBounds> canonical_bounds(const CoreAnalysis& record) {
    const auto& coordinates = record.asset.coordinate_system;
    const auto& source_bounds = preferred_bounds(record);
    if (!coordinates.complete() || !source_bounds.has_value()) {
        return std::nullopt;
    }

    const Vec3 up = axis_vector(*coordinates.up_axis);
    const Vec3 forward = axis_vector(*coordinates.forward_axis);
    if (std::abs(dot(up, forward)) > 0.5) {
        return std::nullopt;
    }
    const Vec3 right = *coordinates.handedness == unified3d::analysis::Handedness::right
        ? cross(forward, up)
        : cross(up, forward);
    const double scale = *coordinates.meters_per_unit;

    CanonicalBounds result;
    result.bounds.min.fill(std::numeric_limits<double>::infinity());
    result.bounds.max.fill(-std::numeric_limits<double>::infinity());
    for (std::size_t corner = 0U; corner < 8U; ++corner) {
        Vec3 source{};
        for (std::size_t axis = 0U; axis < 3U; ++axis) {
            source[axis] = (corner & (std::uint64_t{1U} << axis)) != 0U
                ? source_bounds->max[axis]
                : source_bounds->min[axis];
        }
        const Vec3 canonical{
            dot(source, right) * scale,
            dot(source, up) * scale,
            -dot(source, forward) * scale,
        };
        for (std::size_t axis = 0U; axis < 3U; ++axis) {
            result.bounds.min[axis] = std::min(result.bounds.min[axis], canonical[axis]);
            result.bounds.max[axis] = std::max(result.bounds.max[axis], canonical[axis]);
        }
    }

    double diagonal_squared = 0.0;
    for (std::size_t axis = 0U; axis < 3U; ++axis) {
        result.extents[axis] = result.bounds.max[axis] - result.bounds.min[axis];
        result.center[axis] = (result.bounds.max[axis] + result.bounds.min[axis]) * 0.5;
        diagonal_squared += result.extents[axis] * result.extents[axis];
    }
    result.diagonal = std::sqrt(diagonal_squared);
    return result;
}

struct SpatialComparison {
    double score{};
    double extent_similarity{};
    double center_distance_m{};
    double normalized_center_distance{};
    double center_alignment{};
    std::optional<double> bounds_iou;
};

std::optional<SpatialComparison> compare_spatial_bounds(
    const CoreAnalysis& a,
    const CoreAnalysis& b
) {
    const auto canonical_a = canonical_bounds(a);
    const auto canonical_b = canonical_bounds(b);
    if (!canonical_a.has_value() || !canonical_b.has_value()) {
        return std::nullopt;
    }

    SpatialComparison result;
    for (std::size_t axis = 0U; axis < 3U; ++axis) {
        result.extent_similarity += *ratio_score(
            canonical_a->extents[axis],
            canonical_b->extents[axis]
        );
        const double center_delta = canonical_a->center[axis] - canonical_b->center[axis];
        result.center_distance_m += center_delta * center_delta;
    }
    result.extent_similarity /= 3.0;
    result.center_distance_m = std::sqrt(result.center_distance_m);
    const double reference_diagonal = std::max(
        canonical_a->diagonal,
        canonical_b->diagonal
    );
    result.normalized_center_distance = reference_diagonal > 0.0
        ? result.center_distance_m / reference_diagonal
        : result.center_distance_m == 0.0 ? 0.0
                                          : std::numeric_limits<double>::infinity();
    result.center_alignment = std::isfinite(result.normalized_center_distance)
        ? 1.0 / (1.0 + result.normalized_center_distance)
        : 0.0;

    double intersection_volume = 1.0;
    double volume_a = 1.0;
    double volume_b = 1.0;
    for (std::size_t axis = 0U; axis < 3U; ++axis) {
        intersection_volume *= std::max(
            0.0,
            std::min(canonical_a->bounds.max[axis], canonical_b->bounds.max[axis])
                - std::max(canonical_a->bounds.min[axis], canonical_b->bounds.min[axis])
        );
        volume_a *= canonical_a->extents[axis];
        volume_b *= canonical_b->extents[axis];
    }
    const double union_volume = volume_a + volume_b - intersection_volume;
    if (union_volume > 0.0) {
        result.bounds_iou = intersection_volume / union_volume;
        result.score = 0.5 * result.extent_similarity
            + 0.3 * result.center_alignment
            + 0.2 * *result.bounds_iou;
    } else {
        result.score = 0.625 * result.extent_similarity
            + 0.375 * result.center_alignment;
    }
    return result;
}

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
    result.levels.reserve(8U);
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

    const auto spatial = compare_spatial_bounds(a, b);
    if (!spatial.has_value()) {
        const bool coordinates_available = a.asset.coordinate_system.complete()
            && b.asset.coordinate_system.complete();
        const bool bounds_available = preferred_bounds(a).has_value()
            && preferred_bounds(b).has_value();
        result.levels.push_back(
            ComparisonLevel{
                .level = 7U,
                .name = "spatial_alignment",
                .status = ComparisonLevelStatus::not_available,
                .score = std::nullopt,
                .evidence = {{
                    "reason",
                    std::string{
                        !coordinates_available ? "axes_or_units_not_computed"
                        : !bounds_available ? "bounds_not_computed"
                                            : "invalid_axis_basis"
                    },
                }},
            }
        );
    } else {
        Evidence evidence{
            {"bounds_normalized", true},
            {"canonical_space", std::string{"right_handed_y_up_negative_z_forward_meters"}},
            {"center_alignment", spatial->center_alignment},
            {"center_distance_m", spatial->center_distance_m},
            {"extent_similarity", spatial->extent_similarity},
            {"normalized_center_distance", spatial->normalized_center_distance},
            {"units_normalized", true},
        };
        if (spatial->bounds_iou.has_value()) {
            evidence.emplace("bounds_iou", *spatial->bounds_iou);
        }
        const bool spatial_match = spatial->extent_similarity >= 0.99
            && spatial->normalized_center_distance <= 0.01;
        result.levels.push_back(
            ComparisonLevel{
                .level = 7U,
                .name = "spatial_alignment",
                .status = spatial_match ? ComparisonLevelStatus::match
                                        : ComparisonLevelStatus::different,
                .score = spatial->score,
                .evidence = std::move(evidence),
            }
        );
    }

    std::vector<double> measured_scores;
    measured_scores.reserve(7U);
    for (std::size_t index = 1U; index < result.levels.size(); ++index) {
        if (result.levels[index].score.has_value()) {
            measured_scores.push_back(*result.levels[index].score);
        }
    }
    result.coverage = static_cast<double>(measured_scores.size()) / 7.0;
    if (!measured_scores.empty()) {
        result.score = std::accumulate(measured_scores.begin(), measured_scores.end(), 0.0)
            / static_cast<double>(measured_scores.size());
    }

    if (result.levels[6U].status == ComparisonLevelStatus::match) {
        result.classification = CompatibilityClassification::exact_topology_match;
    } else if (result.levels[7U].status == ComparisonLevelStatus::match) {
        result.classification = donor_detected
            ? CompatibilityClassification::spatial_skin_transfer_required
            : CompatibilityClassification::spatial_match;
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
        ? std::optional<std::size_t>{8U}
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
