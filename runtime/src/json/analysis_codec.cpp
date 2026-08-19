#include "json/analysis_codec.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include <unified3d/core/version.hpp>

namespace unified3d::runtime::json_codec {
namespace {

using namespace unified3d::analysis;
using namespace unified3d::operations::analysis;

class DecodeFailure final : public std::runtime_error {
public:
    explicit DecodeFailure(Diagnostic value)
        : std::runtime_error(value.message), diagnostic(std::move(value)) {}

    Diagnostic diagnostic;
};

[[noreturn]] void fail(
    std::string code,
    std::string message,
    std::string path
) {
    throw DecodeFailure(
        Diagnostic{
            .severity = DiagnosticSeverity::error,
            .code = std::move(code),
            .message = std::move(message),
            .path = std::move(path),
        }
    );
}

void require_object(const Json& value, const std::string& path) {
    if (!value.is_object()) {
        fail("TYPE_OBJECT", "Value must be an object.", path);
    }
}

void require_exact_keys(
    const Json& object,
    const std::initializer_list<std::string_view> keys,
    const std::string& path
) {
    require_object(object, path);
    for (const std::string_view key : keys) {
        if (!object.contains(std::string{key})) {
            fail(
                "REQUIRED_FIELD",
                "Required field is missing; use null when not computed.",
                path + "." + std::string{key}
            );
        }
    }
    for (const auto& [key, unused] : object.items()) {
        static_cast<void>(unused);
        const bool known = std::ranges::any_of(keys, [&key](const std::string_view candidate) {
            return key == candidate;
        });
        if (!known) {
            fail("UNKNOWN_FIELD", "Unknown common-contract field.", path + "." + key);
        }
    }
}

const Json& member(const Json& object, const std::string_view key) {
    return object.at(std::string{key});
}

std::string required_string(
    const Json& object,
    const std::string_view key,
    const std::string& path
) {
    const Json& value = member(object, key);
    if (!value.is_string() || value.get_ref<const std::string&>().empty()) {
        fail("NON_EMPTY_STRING", "Value must be a non-empty string.", path + "." + std::string{key});
    }
    return value.get<std::string>();
}

std::optional<std::string> nullable_string(
    const Json& object,
    const std::string_view key,
    const std::string& path
) {
    const Json& value = member(object, key);
    if (value.is_null()) {
        return std::nullopt;
    }
    if (!value.is_string()) {
        fail("NULLABLE_STRING", "Value must be a string or null.", path + "." + std::string{key});
    }
    return value.get<std::string>();
}

std::optional<Count> nullable_count(
    const Json& object,
    const std::string_view key,
    const std::string& path
) {
    const Json& value = member(object, key);
    if (value.is_null()) {
        return std::nullopt;
    }
    if (!value.is_number_unsigned()) {
        fail(
            "NON_NEGATIVE_INTEGER",
            "Value must be a non-negative integer or null.",
            path + "." + std::string{key}
        );
    }
    return value.get<Count>();
}

std::optional<bool> nullable_bool(
    const Json& object,
    const std::string_view key,
    const std::string& path
) {
    const Json& value = member(object, key);
    if (value.is_null()) {
        return std::nullopt;
    }
    if (!value.is_boolean()) {
        fail("NULLABLE_BOOLEAN", "Value must be a boolean or null.", path + "." + std::string{key});
    }
    return value.get<bool>();
}

std::optional<double> nullable_number(
    const Json& object,
    const std::string_view key,
    const std::string& path
) {
    const Json& value = member(object, key);
    if (value.is_null()) {
        return std::nullopt;
    }
    if (!value.is_number()) {
        fail("NULLABLE_NUMBER", "Value must be a number or null.", path + "." + std::string{key});
    }
    return value.get<double>();
}

std::optional<Handedness> decode_handedness(const Json& coordinate_system) {
    const Json& value = member(coordinate_system, "handedness");
    if (value.is_null()) {
        return std::nullopt;
    }
    if (!value.is_string()) {
        fail("HANDEDNESS", "Handedness must be left, right, or null.", "$.asset.coordinate_system.handedness");
    }
    const std::string text = value.get<std::string>();
    if (text == "left") {
        return Handedness::left;
    }
    if (text == "right") {
        return Handedness::right;
    }
    fail("HANDEDNESS", "Handedness must be left, right, or null.", "$.asset.coordinate_system.handedness");
}

std::optional<Axis> decode_axis(
    const Json& coordinate_system,
    const std::string_view key
) {
    const Json& value = member(coordinate_system, key);
    const std::string path = "$.asset.coordinate_system." + std::string{key};
    if (value.is_null()) {
        return std::nullopt;
    }
    if (!value.is_string()) {
        fail("AXIS", "Axis must be X, -X, Y, -Y, Z, -Z, or null.", path);
    }
    const std::string text = value.get<std::string>();
    if (text == "X") {
        return Axis::positive_x;
    }
    if (text == "-X") {
        return Axis::negative_x;
    }
    if (text == "Y") {
        return Axis::positive_y;
    }
    if (text == "-Y") {
        return Axis::negative_y;
    }
    if (text == "Z") {
        return Axis::positive_z;
    }
    if (text == "-Z") {
        return Axis::negative_z;
    }
    fail("AXIS", "Axis must be X, -X, Y, -Y, Z, -Z, or null.", path);
}

AssetFormat decode_asset_format(const Json& asset) {
    const std::string value = required_string(asset, "format", "$.asset");
    if (value == "fbx") {
        return AssetFormat::fbx;
    }
    if (value == "gltf") {
        return AssetFormat::gltf;
    }
    if (value == "unknown") {
        return AssetFormat::unknown;
    }
    fail("FORMAT", "Unsupported common asset format.", "$.asset.format");
}

AssetContainer decode_asset_container(const Json& asset) {
    const std::string value = required_string(asset, "container", "$.asset");
    if (value == "fbx") {
        return AssetContainer::fbx;
    }
    if (value == "glb") {
        return AssetContainer::glb;
    }
    if (value == "gltf") {
        return AssetContainer::gltf;
    }
    if (value == "unknown") {
        return AssetContainer::unknown;
    }
    fail("CONTAINER", "Unsupported common asset container.", "$.asset.container");
}

std::optional<GeometricVertexSemantic> decode_vertex_semantic(const Json& geometry) {
    const Json& value = member(geometry, "geometric_vertex_semantic");
    if (value.is_null()) {
        return std::nullopt;
    }
    if (!value.is_string()) {
        fail(
            "VERTEX_SEMANTIC",
            "Vertex semantic must be control_points, unique_positions, or null.",
            "$.geometry.geometric_vertex_semantic"
        );
    }
    const std::string text = value.get<std::string>();
    if (text == "control_points") {
        return GeometricVertexSemantic::control_points;
    }
    if (text == "unique_positions") {
        return GeometricVertexSemantic::unique_positions;
    }
    fail(
        "VERTEX_SEMANTIC",
        "Vertex semantic must be control_points, unique_positions, or null.",
        "$.geometry.geometric_vertex_semantic"
    );
}

std::array<double, 3> decode_vector3(const Json& value, const std::string& path) {
    if (!value.is_array() || value.size() != 3U) {
        fail("VECTOR3", "Value must contain exactly three numbers.", path);
    }
    std::array<double, 3> result{};
    for (std::size_t index = 0; index < result.size(); ++index) {
        if (!value[index].is_number()) {
            fail("VECTOR3", "Value must contain exactly three numbers.", path);
        }
        result[index] = value[index].get<double>();
    }
    return result;
}

std::optional<Bounds3d> decode_bounds(const Json& value, const std::string& path) {
    if (value.is_null()) {
        return std::nullopt;
    }
    require_exact_keys(value, {"min", "max"}, path);
    return Bounds3d{
        .min = decode_vector3(member(value, "min"), path + ".min"),
        .max = decode_vector3(member(value, "max"), path + ".max"),
    };
}

std::optional<TopologySignature> decode_topology_signature(const Json& value) {
    if (value.is_null()) {
        return std::nullopt;
    }
    require_exact_keys(value, {"algorithm", "digest", "domain"}, "$.geometry.topology_signature");
    return TopologySignature{
        .algorithm = required_string(value, "algorithm", "$.geometry.topology_signature"),
        .digest = required_string(value, "digest", "$.geometry.topology_signature"),
        .domain = required_string(value, "domain", "$.geometry.topology_signature"),
    };
}

AnalyzerInfo decode_analyzer(const Json& value) {
    require_exact_keys(
        value,
        {"name", "backend", "backend_version", "analyzer_version"},
        "$.analyzer"
    );
    return {
        .name = required_string(value, "name", "$.analyzer"),
        .backend = required_string(value, "backend", "$.analyzer"),
        .backend_version = nullable_string(value, "backend_version", "$.analyzer"),
        .analyzer_version = nullable_string(value, "analyzer_version", "$.analyzer"),
    };
}

AssetInfo decode_asset(const Json& value) {
    require_exact_keys(
        value,
        {"path", "format", "container", "version", "generator", "size_bytes", "coordinate_system"},
        "$.asset"
    );
    const Json& coordinate_system = member(value, "coordinate_system");
    require_exact_keys(
        coordinate_system,
        {"handedness", "up_axis", "forward_axis", "unit", "meters_per_unit"},
        "$.asset.coordinate_system"
    );
    return {
        .path = nullable_string(value, "path", "$.asset"),
        .format = decode_asset_format(value),
        .container = decode_asset_container(value),
        .version = nullable_string(value, "version", "$.asset"),
        .generator = nullable_string(value, "generator", "$.asset"),
        .size_bytes = nullable_count(value, "size_bytes", "$.asset"),
        .coordinate_system = {
            .handedness = decode_handedness(coordinate_system),
            .up_axis = decode_axis(coordinate_system, "up_axis"),
            .forward_axis = decode_axis(coordinate_system, "forward_axis"),
            .unit = nullable_string(coordinate_system, "unit", "$.asset.coordinate_system"),
            .meters_per_unit = nullable_number(
                coordinate_system,
                "meters_per_unit",
                "$.asset.coordinate_system"
            ),
        },
    };
}

SceneAnalysis decode_scene(const Json& value) {
    require_exact_keys(
        value,
        {"scene_count", "node_count", "mesh_instance_count", "bounds"},
        "$.scene"
    );
    return {
        .scene_count = nullable_count(value, "scene_count", "$.scene"),
        .node_count = nullable_count(value, "node_count", "$.scene"),
        .mesh_instance_count = nullable_count(value, "mesh_instance_count", "$.scene"),
        .bounds = decode_bounds(member(value, "bounds"), "$.scene.bounds"),
    };
}

GeometryAnalysis decode_geometry(const Json& value) {
    require_exact_keys(
        value,
        {
            "mesh_count", "primitive_count", "geometric_vertex_count",
            "geometric_vertex_semantic", "render_vertex_count", "index_count",
            "polygon_vertex_count", "polygon_count", "triangle_count",
            "degenerate_triangle_count", "ngon_count", "uv_channel_count",
            "uv_set_binding_count", "normal_count", "tangent_count",
            "color_attribute_count", "bounds", "topology_signature",
        },
        "$.geometry"
    );
    return {
        .mesh_count = nullable_count(value, "mesh_count", "$.geometry"),
        .primitive_count = nullable_count(value, "primitive_count", "$.geometry"),
        .geometric_vertex_count = nullable_count(value, "geometric_vertex_count", "$.geometry"),
        .geometric_vertex_semantic = decode_vertex_semantic(value),
        .render_vertex_count = nullable_count(value, "render_vertex_count", "$.geometry"),
        .index_count = nullable_count(value, "index_count", "$.geometry"),
        .polygon_vertex_count = nullable_count(value, "polygon_vertex_count", "$.geometry"),
        .polygon_count = nullable_count(value, "polygon_count", "$.geometry"),
        .triangle_count = nullable_count(value, "triangle_count", "$.geometry"),
        .degenerate_triangle_count = nullable_count(value, "degenerate_triangle_count", "$.geometry"),
        .ngon_count = nullable_count(value, "ngon_count", "$.geometry"),
        .uv_channel_count = nullable_count(value, "uv_channel_count", "$.geometry"),
        .uv_set_binding_count = nullable_count(value, "uv_set_binding_count", "$.geometry"),
        .normal_count = nullable_count(value, "normal_count", "$.geometry"),
        .tangent_count = nullable_count(value, "tangent_count", "$.geometry"),
        .color_attribute_count = nullable_count(value, "color_attribute_count", "$.geometry"),
        .bounds = decode_bounds(member(value, "bounds"), "$.geometry.bounds"),
        .topology_signature = decode_topology_signature(member(value, "topology_signature")),
    };
}

MaterialsAnalysis decode_materials(const Json& value) {
    require_exact_keys(
        value,
        {"material_resource_count", "material_binding_count", "texture_resource_count"},
        "$.materials"
    );
    return {
        .material_resource_count = nullable_count(value, "material_resource_count", "$.materials"),
        .material_binding_count = nullable_count(value, "material_binding_count", "$.materials"),
        .texture_resource_count = nullable_count(value, "texture_resource_count", "$.materials"),
    };
}

std::optional<std::vector<std::string>> nullable_string_array(
    const Json& object,
    const std::string_view key,
    const std::string& path
) {
    const Json& value = member(object, key);
    if (value.is_null()) {
        return std::nullopt;
    }
    if (!value.is_array()) {
        fail("STRING_ARRAY", "Value must be an array of strings or null.", path + "." + std::string{key});
    }
    std::vector<std::string> result;
    result.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (!value[index].is_string()) {
            fail(
                "STRING_ARRAY",
                "Value must be an array of strings or null.",
                path + "." + std::string{key} + "[" + std::to_string(index) + "]"
            );
        }
        result.push_back(value[index].get<std::string>());
    }
    return result;
}

SkeletonAnalysis decode_skeleton(const Json& value) {
    require_exact_keys(
        value,
        {"present", "joint_count", "root_joint_count", "hierarchy_signature", "bind_pose_signature", "joint_names"},
        "$.skeleton"
    );
    return {
        .present = nullable_bool(value, "present", "$.skeleton"),
        .joint_count = nullable_count(value, "joint_count", "$.skeleton"),
        .root_joint_count = nullable_count(value, "root_joint_count", "$.skeleton"),
        .hierarchy_signature = nullable_string(value, "hierarchy_signature", "$.skeleton"),
        .bind_pose_signature = nullable_string(value, "bind_pose_signature", "$.skeleton"),
        .joint_names = nullable_string_array(value, "joint_names", "$.skeleton"),
    };
}

SkinAnalysis decode_skin(const Json& value) {
    require_exact_keys(
        value,
        {"present", "skin_count", "cluster_count", "skinned_vertex_count", "max_influences", "influence_set_count"},
        "$.skin"
    );
    return {
        .present = nullable_bool(value, "present", "$.skin"),
        .skin_count = nullable_count(value, "skin_count", "$.skin"),
        .cluster_count = nullable_count(value, "cluster_count", "$.skin"),
        .skinned_vertex_count = nullable_count(value, "skinned_vertex_count", "$.skin"),
        .max_influences = nullable_count(value, "max_influences", "$.skin"),
        .influence_set_count = nullable_count(value, "influence_set_count", "$.skin"),
    };
}

AnimationAnalysis decode_animation(const Json& value) {
    require_exact_keys(
        value,
        {"present", "clip_count", "channel_count", "sampler_count", "duration_seconds"},
        "$.animation"
    );
    return {
        .present = nullable_bool(value, "present", "$.animation"),
        .clip_count = nullable_count(value, "clip_count", "$.animation"),
        .channel_count = nullable_count(value, "channel_count", "$.animation"),
        .sampler_count = nullable_count(value, "sampler_count", "$.animation"),
        .duration_seconds = nullable_number(value, "duration_seconds", "$.animation"),
    };
}

NativeSections decode_native(const Json& value) {
    require_object(value, "$.native");
    NativeSections result;
    result.reserve(value.size());
    for (const auto& [name, native_value] : value.items()) {
        const std::string serialized = native_value.dump();
        NativePayload payload;
        payload.bytes.resize(serialized.size());
        if (!serialized.empty()) {
            std::memcpy(payload.bytes.data(), serialized.data(), serialized.size());
        }
        result.emplace(name, std::move(payload));
    }
    return result;
}

DiagnosticSeverity decode_severity(const Json& value, const std::string& path) {
    if (!value.is_string()) {
        fail("DIAGNOSTIC_SEVERITY", "Diagnostic severity must be info, warning, or error.", path);
    }
    const std::string text = value.get<std::string>();
    if (text == "info") {
        return DiagnosticSeverity::info;
    }
    if (text == "warning") {
        return DiagnosticSeverity::warning;
    }
    if (text == "error") {
        return DiagnosticSeverity::error;
    }
    fail("DIAGNOSTIC_SEVERITY", "Diagnostic severity must be info, warning, or error.", path);
}

std::vector<Diagnostic> decode_diagnostics(const Json& value) {
    if (!value.is_array()) {
        fail("TYPE_ARRAY", "Diagnostics must be an array.", "$.diagnostics");
    }
    std::vector<Diagnostic> result;
    result.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        const std::string path = "$.diagnostics[" + std::to_string(index) + "]";
        const Json& diagnostic = value[index];
        require_exact_keys(diagnostic, {"severity", "code", "message", "path"}, path);
        const auto source_path = nullable_string(diagnostic, "path", path);
        result.push_back(
            Diagnostic{
                .severity = decode_severity(member(diagnostic, "severity"), path + ".severity"),
                .code = required_string(diagnostic, "code", path),
                .message = required_string(diagnostic, "message", path),
                .path = source_path.value_or(""),
            }
        );
    }
    return result;
}

std::string severity_name(const DiagnosticSeverity severity) {
    switch (severity) {
        case DiagnosticSeverity::info:
            return "info";
        case DiagnosticSeverity::warning:
            return "warning";
        case DiagnosticSeverity::error:
            return "error";
    }
    return "error";
}

std::string level_status_name(const ComparisonLevelStatus status) {
    switch (status) {
        case ComparisonLevelStatus::match:
            return "match";
        case ComparisonLevelStatus::different:
            return "different";
        case ComparisonLevelStatus::not_comparable:
            return "not_comparable";
        case ComparisonLevelStatus::not_available:
            return "not_available";
    }
    return "not_available";
}

std::string classification_name(const CompatibilityClassification classification) {
    switch (classification) {
        case CompatibilityClassification::exact_topology_match:
            return "EXACT_TOPOLOGY_MATCH";
        case CompatibilityClassification::direct_skin_transfer_compatible:
            return "DIRECT_SKIN_TRANSFER_COMPATIBLE";
        case CompatibilityClassification::geometry_match:
            return "GEOMETRY_MATCH";
        case CompatibilityClassification::geometric_vertex_mapping_required:
            return "GEOMETRIC_VERTEX_MAPPING_REQUIRED";
        case CompatibilityClassification::spatial_match:
            return "SPATIAL_MATCH";
        case CompatibilityClassification::spatial_skin_transfer_required:
            return "SPATIAL_SKIN_TRANSFER_REQUIRED";
        case CompatibilityClassification::similar_geometry:
            return "SIMILAR_GEOMETRY";
        case CompatibilityClassification::advanced_transfer_required:
            return "ADVANCED_TRANSFER_REQUIRED";
        case CompatibilityClassification::incompatible:
            return "INCOMPATIBLE";
    }
    return "INCOMPATIBLE";
}

Json nullable_json(const std::optional<double> value) {
    return value.has_value() ? Json(*value) : Json(nullptr);
}

Json nullable_json(const std::optional<std::size_t> value) {
    return value.has_value() ? Json(*value) : Json(nullptr);
}

Json nullable_json(const std::optional<bool> value) {
    return value.has_value() ? Json(*value) : Json(nullptr);
}

Json encode_evidence_value(const EvidenceValue& value) {
    return std::visit([](const auto& current) -> Json { return Json(current); }, value);
}

}  // namespace

bool DecodeAnalysisResult::valid() const noexcept {
    return record.has_value()
        && std::ranges::none_of(diagnostics, [](const Diagnostic& diagnostic) {
            return diagnostic.severity == DiagnosticSeverity::error;
        });
}

DecodeAnalysisResult decode_analysis_record(const Json& value) {
    DecodeAnalysisResult result;
    try {
        require_exact_keys(
            value,
            {
                "status", "schema", "schema_status", "analyzer", "asset", "scene",
                "geometry", "materials", "skeleton", "skin", "animation", "native",
                "diagnostics",
            },
            "$"
        );
        AnalysisRecord record;
        record.status = required_string(value, "status", "$");
        record.schema = required_string(value, "schema", "$");
        record.schema_status = required_string(value, "schema_status", "$");
        record.analyzer = decode_analyzer(member(value, "analyzer"));
        record.asset = decode_asset(member(value, "asset"));
        record.scene = decode_scene(member(value, "scene"));
        record.geometry = decode_geometry(member(value, "geometry"));
        record.materials = decode_materials(member(value, "materials"));
        record.skeleton = decode_skeleton(member(value, "skeleton"));
        record.skin = decode_skin(member(value, "skin"));
        record.animation = decode_animation(member(value, "animation"));
        record.native = decode_native(member(value, "native"));
        record.diagnostics = decode_diagnostics(member(value, "diagnostics"));

        const ValidationResult semantic = validate_analysis_record(record);
        result.diagnostics = semantic.diagnostics;
        result.record = std::move(record);
    } catch (const DecodeFailure& error) {
        result.diagnostics.push_back(error.diagnostic);
    } catch (const std::exception& error) {
        result.diagnostics.push_back(
            Diagnostic{
                .severity = DiagnosticSeverity::error,
                .code = "DECODE_ERROR",
                .message = error.what(),
                .path = "$",
            }
        );
    }
    return result;
}

Json encode_diagnostic(const Diagnostic& diagnostic) {
    return Json{
        {"severity", severity_name(diagnostic.severity)},
        {"code", diagnostic.code},
        {"message", diagnostic.message},
        {"path", diagnostic.path.empty() ? Json(nullptr) : Json(diagnostic.path)},
    };
}

Json encode_diagnostics(const std::vector<Diagnostic>& diagnostics) {
    Json result = Json::array();
    for (const Diagnostic& diagnostic : diagnostics) {
        result.push_back(encode_diagnostic(diagnostic));
    }
    return result;
}

Json encode_analysis_comparison(
    const AnalysisComparison& comparison,
    const Json& input_a,
    const Json& input_b
) {
    Json pattern{
        {"detected", comparison.rig_donor_geometry_target_pattern.detected},
        {"donor", nullptr},
        {"target", nullptr},
        {
            "recommended_next_check",
            comparison.rig_donor_geometry_target_pattern.recommended_next_check.has_value()
                ? Json(*comparison.rig_donor_geometry_target_pattern.recommended_next_check)
                : Json(nullptr)
        },
    };
    if (comparison.rig_donor_geometry_target_pattern.donor.has_value()) {
        pattern["donor"] = *comparison.rig_donor_geometry_target_pattern.donor == InputSide::a
            ? "a"
            : "b";
    }
    if (comparison.rig_donor_geometry_target_pattern.target.has_value()) {
        pattern["target"] = *comparison.rig_donor_geometry_target_pattern.target == InputSide::a
            ? "a"
            : "b";
    }

    Json levels = Json::array();
    for (const ComparisonLevel& level : comparison.compatibility.levels) {
        Json evidence = Json::object();
        for (const auto& [key, value] : level.evidence) {
            evidence[key] = encode_evidence_value(value);
        }
        levels.push_back(
            Json{
                {"level", level.level},
                {"name", level.name},
                {"status", level_status_name(level.status)},
                {"score", nullable_json(level.score)},
                {"evidence", std::move(evidence)},
            }
        );
    }

    const Json classification = comparison.compatibility.classification.has_value()
        ? Json(classification_name(*comparison.compatibility.classification))
        : Json(nullptr);
    return Json{
        {"status", "ok"},
        {"schema", unified3d::analysis_comparison_schema},
        {"inputs", {{"a", input_a}, {"b", input_b}}},
        {
            "comparison",
            {
                {"same_mesh_count", nullable_json(comparison.same_mesh_count)},
                {"triangle_ratio_b_over_a", nullable_json(comparison.triangle_ratio_b_over_a)},
                {
                    "index_transfer_ruled_out_by_triangle_count",
                    comparison.index_transfer_ruled_out_by_triangle_count
                },
                {"rig_donor_geometry_target_pattern", std::move(pattern)},
                {"topology_signatures_comparable", comparison.topology_signatures_comparable},
                {
                    "compatibility",
                    {
                        {"classification", classification},
                        {"score", nullable_json(comparison.compatibility.score)},
                        {"coverage", comparison.compatibility.coverage},
                        {"levels", std::move(levels)},
                        {
                            "recommended_next_level",
                            nullable_json(comparison.compatibility.recommended_next_level)
                        },
                    }
                },
            }
        },
    };
}

}  // namespace unified3d::runtime::json_codec
