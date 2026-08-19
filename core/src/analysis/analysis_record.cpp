#include <unified3d/core/analysis/analysis_record.hpp>

#include <algorithm>
#include <cmath>
#include <string_view>

namespace unified3d::analysis {
namespace {

int axis_family(const Axis axis) {
    switch (axis) {
        case Axis::positive_x:
        case Axis::negative_x:
            return 0;
        case Axis::positive_y:
        case Axis::negative_y:
            return 1;
        case Axis::positive_z:
        case Axis::negative_z:
            return 2;
    }
    return -1;
}

void add_diagnostic(
    ValidationResult& result,
    const DiagnosticSeverity severity,
    std::string code,
    std::string message,
    std::string path
) {
    result.diagnostics.push_back(
        Diagnostic{
            .severity = severity,
            .code = std::move(code),
            .message = std::move(message),
            .path = std::move(path),
        }
    );
}

void validate_bounds(
    ValidationResult& result,
    const std::optional<Bounds3d>& bounds,
    const std::string_view path
) {
    if (!bounds.has_value()) {
        return;
    }

    for (std::size_t axis = 0; axis < bounds->min.size(); ++axis) {
        if (!std::isfinite(bounds->min[axis]) || !std::isfinite(bounds->max[axis])) {
            add_diagnostic(
                result,
                DiagnosticSeverity::error,
                "BOUNDS_FINITE",
                "Bounds components must be finite.",
                std::string{path}
            );
            return;
        }
        if (bounds->min[axis] > bounds->max[axis]) {
            add_diagnostic(
                result,
                DiagnosticSeverity::error,
                "BOUNDS_ORDER",
                "Each bounds min component must be less than or equal to max.",
                std::string{path}
            );
            return;
        }
    }
}

void validate_presence_count(
    ValidationResult& result,
    const std::optional<bool> present,
    const std::optional<Count> count,
    const std::string_view section,
    const std::string_view count_name
) {
    if (present == false && count.has_value() && *count != 0U) {
        add_diagnostic(
            result,
            DiagnosticSeverity::error,
            "PRESENCE_COUNT",
            "present=false requires " + std::string{count_name} + "=0 or null.",
            "$." + std::string{section}
        );
    }
    if (present == true && count == 0U) {
        add_diagnostic(
            result,
            DiagnosticSeverity::error,
            "PRESENCE_COUNT",
            "present=true conflicts with " + std::string{count_name} + "=0.",
            "$." + std::string{section}
        );
    }
}

}  // namespace

bool CoordinateSystem::complete() const noexcept {
    return handedness.has_value() && up_axis.has_value() && forward_axis.has_value()
        && meters_per_unit.has_value();
}

ValidationResult validate_analysis_record(const AnalysisRecord& record) {
    ValidationResult result;

    if (record.status != "ok") {
        add_diagnostic(
            result,
            DiagnosticSeverity::error,
            "STATUS",
            "Successful analysis records must use status 'ok'.",
            "$.status"
        );
    }
    if (record.schema != unified3d::analysis_schema) {
        add_diagnostic(
            result,
            DiagnosticSeverity::error,
            "SCHEMA",
            "Unsupported analysis schema identifier.",
            "$.schema"
        );
    }
    if (record.schema_status != "release-candidate") {
        add_diagnostic(
            result,
            DiagnosticSeverity::error,
            "SCHEMA_STATUS",
            "Analysis schema status must be release-candidate.",
            "$.schema_status"
        );
    }
    if (record.analyzer.name.empty()) {
        add_diagnostic(
            result,
            DiagnosticSeverity::error,
            "NON_EMPTY_STRING",
            "Analyzer name must not be empty.",
            "$.analyzer.name"
        );
    }
    if (record.analyzer.backend.empty()) {
        add_diagnostic(
            result,
            DiagnosticSeverity::error,
            "NON_EMPTY_STRING",
            "Analyzer backend must not be empty.",
            "$.analyzer.backend"
        );
    }

    const bool format_container_valid =
        (record.asset.format == AssetFormat::fbx
         && record.asset.container == AssetContainer::fbx)
        || (record.asset.format == AssetFormat::gltf
            && (record.asset.container == AssetContainer::glb
                || record.asset.container == AssetContainer::gltf))
        || (record.asset.format == AssetFormat::unknown
            && record.asset.container == AssetContainer::unknown);
    if (!format_container_valid) {
        add_diagnostic(
            result,
            DiagnosticSeverity::error,
            "FORMAT_CONTAINER",
            "Asset format and container are inconsistent.",
            "$.asset.container"
        );
    }

    if (record.asset.coordinate_system.meters_per_unit.has_value()
        && (!std::isfinite(*record.asset.coordinate_system.meters_per_unit)
            || *record.asset.coordinate_system.meters_per_unit <= 0.0)) {
        add_diagnostic(
            result,
            DiagnosticSeverity::error,
            "UNIT_SCALE",
            "meters_per_unit must be a finite positive number.",
            "$.asset.coordinate_system.meters_per_unit"
        );
    }
    if (!record.asset.coordinate_system.complete()) {
        add_diagnostic(
            result,
            DiagnosticSeverity::warning,
            "COORDINATE_SYSTEM_UNKNOWN",
            "Spatial compatibility cannot be evaluated until axes and units are known.",
            "$.asset.coordinate_system"
        );
    } else if (axis_family(*record.asset.coordinate_system.up_axis)
               == axis_family(*record.asset.coordinate_system.forward_axis)) {
        add_diagnostic(
            result,
            DiagnosticSeverity::error,
            "AXIS_BASIS",
            "Up and forward axes must be orthogonal.",
            "$.asset.coordinate_system"
        );
    }

    validate_bounds(result, record.scene.bounds, "$.scene.bounds");
    validate_bounds(result, record.geometry.bounds, "$.geometry.bounds");

    if (record.geometry.geometric_vertex_count.has_value()
        && !record.geometry.geometric_vertex_semantic.has_value()) {
        add_diagnostic(
            result,
            DiagnosticSeverity::error,
            "VERTEX_SEMANTIC",
            "A geometric vertex count requires an explicit semantic.",
            "$.geometry.geometric_vertex_semantic"
        );
    }
    if (record.geometry.degenerate_triangle_count.has_value()
        && record.geometry.triangle_count.has_value()
        && *record.geometry.degenerate_triangle_count > *record.geometry.triangle_count) {
        add_diagnostic(
            result,
            DiagnosticSeverity::error,
            "COUNT_RELATION",
            "Degenerate triangle count cannot exceed triangle count.",
            "$.geometry.degenerate_triangle_count"
        );
    }
    if (record.geometry.topology_signature.has_value()) {
        const auto& signature = *record.geometry.topology_signature;
        if (signature.algorithm.empty() || signature.digest.empty() || signature.domain.empty()) {
            add_diagnostic(
                result,
                DiagnosticSeverity::error,
                "TOPOLOGY_SIGNATURE",
                "Topology signature fields must not be empty.",
                "$.geometry.topology_signature"
            );
        }
    }

    validate_presence_count(
        result,
        record.skeleton.present,
        record.skeleton.joint_count,
        "skeleton",
        "joint_count"
    );
    validate_presence_count(
        result,
        record.skin.present,
        record.skin.skin_count,
        "skin",
        "skin_count"
    );
    validate_presence_count(
        result,
        record.animation.present,
        record.animation.clip_count,
        "animation",
        "clip_count"
    );

    if (record.skeleton.root_joint_count.has_value()
        && record.skeleton.joint_count.has_value()
        && *record.skeleton.root_joint_count > *record.skeleton.joint_count) {
        add_diagnostic(
            result,
            DiagnosticSeverity::error,
            "COUNT_RELATION",
            "Root joint count cannot exceed joint count.",
            "$.skeleton.root_joint_count"
        );
    }
    if (record.skeleton.joint_names.has_value()
        && record.skeleton.joint_count.has_value()
        && !record.skeleton.joint_names->empty()
        && record.skeleton.joint_names->size() != *record.skeleton.joint_count) {
        add_diagnostic(
            result,
            DiagnosticSeverity::warning,
            "JOINT_NAME_COVERAGE",
            "Joint-name inventory is partial.",
            "$.skeleton.joint_names"
        );
    }

    if (record.skin.max_influences.has_value()
        && record.skin.influence_set_count.has_value()) {
        const Count required_sets = (*record.skin.max_influences + 3U) / 4U;
        if (*record.skin.influence_set_count < required_sets) {
            add_diagnostic(
                result,
                DiagnosticSeverity::error,
                "INFLUENCE_SET_CAPACITY",
                "Influence-set count cannot represent max_influences.",
                "$.skin.influence_set_count"
            );
        }
    }

    if (record.animation.duration_seconds.has_value()
        && (!std::isfinite(*record.animation.duration_seconds)
            || *record.animation.duration_seconds < 0.0)) {
        add_diagnostic(
            result,
            DiagnosticSeverity::error,
            "DURATION",
            "Animation duration must be finite and non-negative.",
            "$.animation.duration_seconds"
        );
    }

    return result;
}

}  // namespace unified3d::analysis
