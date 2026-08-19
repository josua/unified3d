#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <unified3d/core/diagnostic.hpp>
#include <unified3d/core/version.hpp>

namespace unified3d::analysis {

using Count = std::uint64_t;

enum class AssetFormat {
    fbx,
    gltf,
    unknown,
};

enum class AssetContainer {
    fbx,
    glb,
    gltf,
    unknown,
};

enum class Handedness {
    left,
    right,
};

enum class Axis {
    positive_x,
    negative_x,
    positive_y,
    negative_y,
    positive_z,
    negative_z,
};

enum class GeometricVertexSemantic {
    control_points,
    unique_positions,
};

struct AnalyzerInfo {
    std::string name;
    std::string backend;
    std::optional<std::string> backend_version;
    std::optional<std::string> analyzer_version;
};

struct CoordinateSystem {
    std::optional<Handedness> handedness;
    std::optional<Axis> up_axis;
    std::optional<Axis> forward_axis;
    std::optional<std::string> unit;
    std::optional<double> meters_per_unit;

    [[nodiscard]] bool complete() const noexcept;
    [[nodiscard]] bool operator==(const CoordinateSystem&) const = default;
};

struct Bounds3d {
    std::array<double, 3> min{};
    std::array<double, 3> max{};

    [[nodiscard]] bool operator==(const Bounds3d&) const = default;
};

struct AssetInfo {
    std::optional<std::string> path;
    AssetFormat format{AssetFormat::unknown};
    AssetContainer container{AssetContainer::unknown};
    std::optional<std::string> version;
    std::optional<std::string> generator;
    std::optional<Count> size_bytes;
    CoordinateSystem coordinate_system;
};

struct SceneAnalysis {
    std::optional<Count> scene_count;
    std::optional<Count> node_count;
    std::optional<Count> mesh_instance_count;
    std::optional<Bounds3d> bounds;
};

struct TopologySignature {
    std::string algorithm;
    std::string digest;
    std::string domain;
};

struct GeometryAnalysis {
    std::optional<Count> mesh_count;
    std::optional<Count> primitive_count;
    std::optional<Count> geometric_vertex_count;
    std::optional<GeometricVertexSemantic> geometric_vertex_semantic;
    std::optional<Count> render_vertex_count;
    std::optional<Count> index_count;
    std::optional<Count> polygon_vertex_count;
    std::optional<Count> polygon_count;
    std::optional<Count> triangle_count;
    std::optional<Count> degenerate_triangle_count;
    std::optional<Count> ngon_count;
    std::optional<Count> uv_channel_count;
    std::optional<Count> uv_set_binding_count;
    std::optional<Count> normal_count;
    std::optional<Count> tangent_count;
    std::optional<Count> color_attribute_count;
    std::optional<Bounds3d> bounds;
    std::optional<TopologySignature> topology_signature;
};

struct MaterialsAnalysis {
    std::optional<Count> material_resource_count;
    std::optional<Count> material_binding_count;
    std::optional<Count> texture_resource_count;
};

struct SkeletonAnalysis {
    std::optional<bool> present;
    std::optional<Count> joint_count;
    std::optional<Count> root_joint_count;
    std::optional<std::string> hierarchy_signature;
    std::optional<std::string> bind_pose_signature;
    std::optional<std::vector<std::string>> joint_names;
};

struct SkinAnalysis {
    std::optional<bool> present;
    std::optional<Count> skin_count;
    std::optional<Count> cluster_count;
    std::optional<Count> skinned_vertex_count;
    std::optional<Count> max_influences;
    std::optional<Count> influence_set_count;
};

struct AnimationAnalysis {
    std::optional<bool> present;
    std::optional<Count> clip_count;
    std::optional<Count> channel_count;
    std::optional<Count> sampler_count;
    std::optional<double> duration_seconds;
};

struct NativePayload {
    std::string content_type{"application/json"};
    std::vector<std::byte> bytes;
};

using NativeSections = std::unordered_map<std::string, NativePayload>;

struct AnalysisRecord {
    std::string status{"ok"};
    std::string schema{unified3d::analysis_schema};
    std::string schema_status{"release-candidate"};
    AnalyzerInfo analyzer;
    AssetInfo asset;
    SceneAnalysis scene;
    GeometryAnalysis geometry;
    MaterialsAnalysis materials;
    SkeletonAnalysis skeleton;
    SkinAnalysis skin;
    AnimationAnalysis animation;
    NativeSections native;
    std::vector<Diagnostic> diagnostics;
};

[[nodiscard]] ValidationResult validate_analysis_record(const AnalysisRecord& record);

}  // namespace unified3d::analysis
