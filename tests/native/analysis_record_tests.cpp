#include <unified3d/core/analysis/analysis_record.hpp>
#include <unified3d/core/geometry/buffers.hpp>
#include <unified3d/core/geometry/canonical_fingerprint.hpp>
#include <unified3d/core/geometry/spatial_skin_transfer.hpp>
#include <unified3d/operations/analysis/compare_analysis_records.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <span>
#include <vector>

namespace {

using unified3d::DiagnosticSeverity;
using unified3d::analysis::AnalysisRecord;
using unified3d::analysis::Axis;
using unified3d::analysis::AssetContainer;
using unified3d::analysis::AssetFormat;
using unified3d::analysis::Bounds3d;
using unified3d::analysis::GeometricVertexSemantic;
using unified3d::analysis::Handedness;
using unified3d::analysis::TopologySignature;
using unified3d::operations::analysis::CompatibilityClassification;
using unified3d::operations::analysis::ComparisonLevelStatus;
using unified3d::operations::analysis::InputSide;
using unified3d::geometry::BufferView;
using unified3d::geometry::IndexBuffer;
using unified3d::geometry::PrimitiveBuffers;
using unified3d::geometry::ScalarType;
using unified3d::geometry::SkinInfluenceSet;
using unified3d::geometry::SkinTransferBuffers;
using unified3d::geometry::VertexAttributeBuffer;
using unified3d::geometry::VertexSemantic;

int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (condition) {
        return;
    }
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

void expect_near(
    const double actual,
    const double expected,
    const double tolerance,
    const std::string_view message
) {
    expect(std::abs(actual - expected) <= tolerance, message);
}

AnalysisRecord thief_fbx() {
    AnalysisRecord record;
    record.analyzer = {
        .name = "FBX Geometry Rig Analyzer",
        .backend = "Autodesk FBX SDK",
        .backend_version = std::nullopt,
        .analyzer_version = "1.0.0",
    };
    record.asset.path = "C:\\assets\\thief-walking.fbx";
    record.asset.format = AssetFormat::fbx;
    record.asset.container = AssetContainer::fbx;
    record.asset.version = "7.7.0";
    record.asset.generator = "Autodesk FBX SDK";
    record.asset.size_bytes = 88465216U;

    record.geometry.mesh_count = 10U;
    record.geometry.geometric_vertex_count = 27311U;
    record.geometry.geometric_vertex_semantic = GeometricVertexSemantic::control_points;
    record.geometry.polygon_vertex_count = 150177U;
    record.geometry.polygon_count = 50059U;
    record.geometry.triangle_count = 50059U;
    record.geometry.uv_set_binding_count = 10U;
    record.geometry.topology_signature = TopologySignature{
        .algorithm = "fbx_adapter_topology",
        .digest = "fd17e9faa2426bc8",
        .domain = "adapter-local-decoded-topology",
    };

    record.materials.material_resource_count = 1U;
    record.materials.texture_resource_count = 3U;

    record.skeleton.present = true;
    record.skeleton.joint_count = 41U;
    record.skin.present = true;
    record.skin.skin_count = 10U;
    record.skin.cluster_count = 75U;
    record.skin.max_influences = 6U;
    record.skin.influence_set_count = 2U;
    record.animation.present = true;
    record.animation.clip_count = 1U;
    return record;
}

AnalysisRecord thief_glb() {
    AnalysisRecord record;
    record.analyzer = {
        .name = "GLB Geometry Rig Analyzer",
        .backend = "glTF-Transform",
        .backend_version = "4.4.2",
        .analyzer_version = "0.1.0",
    };
    record.asset.path = "C:\\assets\\thief.glb";
    record.asset.format = AssetFormat::gltf;
    record.asset.container = AssetContainer::glb;
    record.asset.version = "2.0";
    record.asset.generator = "Tripo";

    record.scene.mesh_instance_count = 10U;
    record.geometry.mesh_count = 10U;
    record.geometry.primitive_count = 10U;
    record.geometry.geometric_vertex_count = 983579U;
    record.geometry.geometric_vertex_semantic = GeometricVertexSemantic::unique_positions;
    record.geometry.render_vertex_count = 1045852U;
    record.geometry.index_count = 5871732U;
    record.geometry.triangle_count = 1957244U;
    record.geometry.degenerate_triangle_count = 0U;
    record.geometry.ngon_count = 0U;
    record.geometry.uv_channel_count = 1U;
    record.geometry.topology_signature = TopologySignature{
        .algorithm = "decoded_gltf_topology_sha256",
        .digest = "adf37d930c59a58b1f2be3890623de660f2e47aaf453578b65d487d5c53fbbed",
        .domain = "adapter-local-decoded-topology",
    };

    record.materials.material_resource_count = 10U;
    record.materials.material_binding_count = 10U;
    record.materials.texture_resource_count = 30U;

    record.skeleton.present = false;
    record.skeleton.joint_count = 0U;
    record.skeleton.root_joint_count = 0U;
    record.skeleton.joint_names = std::vector<std::string>{};
    record.skin.present = false;
    record.skin.skin_count = 0U;
    record.skin.cluster_count = 0U;
    record.skin.skinned_vertex_count = 0U;
    record.skin.max_influences = 0U;
    record.skin.influence_set_count = 0U;
    record.animation.present = false;
    record.animation.clip_count = 0U;
    record.animation.channel_count = 0U;
    record.animation.sampler_count = 0U;
    record.animation.duration_seconds = 0.0;
    return record;
}

void validation_tests() {
    const auto fbx_validation = unified3d::analysis::validate_analysis_record(thief_fbx());
    const auto glb_validation = unified3d::analysis::validate_analysis_record(thief_glb());
    expect(fbx_validation.valid(), "FBX regression record must be valid");
    expect(glb_validation.valid(), "GLB regression record must be valid");
    expect(fbx_validation.diagnostics.size() == 1U, "FBX must report one coordinate warning");
    expect(glb_validation.diagnostics.size() == 1U, "GLB must report one coordinate warning");
    expect(
        fbx_validation.diagnostics.front().code == "COORDINATE_SYSTEM_UNKNOWN",
        "FBX warning code must match the Python contract"
    );

    auto invalid_skin = thief_fbx();
    invalid_skin.skin.present = false;
    const auto invalid_skin_result =
        unified3d::analysis::validate_analysis_record(invalid_skin);
    expect(!invalid_skin_result.valid(), "present=false with ten skins must be rejected");

    auto insufficient_sets = thief_fbx();
    insufficient_sets.skin.influence_set_count = 1U;
    const auto insufficient_sets_result =
        unified3d::analysis::validate_analysis_record(insufficient_sets);
    expect(!insufficient_sets_result.valid(), "one influence set cannot preserve six influences");

    auto parallel_axes = thief_glb();
    parallel_axes.asset.coordinate_system = {
        .handedness = Handedness::right,
        .up_axis = Axis::positive_y,
        .forward_axis = Axis::negative_y,
        .unit = "m",
        .meters_per_unit = 1.0,
    };
    const auto parallel_axes_result =
        unified3d::analysis::validate_analysis_record(parallel_axes);
    expect(!parallel_axes_result.valid(), "parallel up and forward axes must be rejected");
}

void comparison_parity_tests() {
    const auto result = unified3d::operations::analysis::compare_analysis_records(
        thief_fbx(),
        thief_glb()
    );
    expect(result.success(), "FBX-to-GLB comparison must succeed");
    expect(result.comparison.has_value(), "successful comparison must contain a value");
    const auto& comparison = *result.comparison;
    expect(comparison.same_mesh_count == true, "both records must contain ten meshes");
    expect(
        comparison.index_transfer_ruled_out_by_triangle_count,
        "different triangle counts must rule out direct index transfer"
    );
    expect(comparison.triangle_ratio_b_over_a.has_value(), "triangle ratio must be measured");
    expect_near(
        *comparison.triangle_ratio_b_over_a,
        39.09874348269043,
        1.0e-12,
        "native triangle ratio must match Python"
    );
    expect(
        comparison.rig_donor_geometry_target_pattern.detected,
        "rig donor and dense target pattern must be detected"
    );
    expect(
        comparison.rig_donor_geometry_target_pattern.donor == InputSide::a,
        "FBX input A must be the rig donor"
    );
    expect(
        comparison.rig_donor_geometry_target_pattern.target == InputSide::b,
        "GLB input B must be the geometry target"
    );
    expect(
        !comparison.topology_signatures_comparable,
        "adapter-local signatures with different algorithms must not be compared"
    );

    const auto& compatibility = comparison.compatibility;
    expect(compatibility.levels.size() == 8U, "compatibility must contain levels zero through seven");
    expect(
        compatibility.classification
            == CompatibilityClassification::advanced_transfer_required,
        "thief transfer must require advanced mapping"
    );
    expect(compatibility.recommended_next_level == 1U, "coordinate system is the next unresolved level");
    expect_near(compatibility.coverage, 2.0 / 7.0, 1.0e-15, "coverage must match Python");
    expect(compatibility.score.has_value(), "partial compatibility score must exist");
    expect_near(
        *compatibility.score,
        0.5127881347445694,
        1.0e-15,
        "partial compatibility score must match Python"
    );
    expect(
        compatibility.levels[3U].status == ComparisonLevelStatus::match,
        "mesh structure level must match"
    );
    expect(
        compatibility.levels[4U].status == ComparisonLevelStatus::different,
        "triangle statistics level must differ"
    );
    expect(
        compatibility.levels[5U].status == ComparisonLevelStatus::not_comparable,
        "control points and render vertices must not be compared"
    );
    expect(
        compatibility.levels[6U].status == ComparisonLevelStatus::not_comparable,
        "different signature algorithms must not be compared"
    );
    expect(
        compatibility.levels[7U].status == ComparisonLevelStatus::not_available,
        "spatial alignment must remain unavailable without axes and bounds"
    );

    auto spatial_fbx = thief_fbx();
    spatial_fbx.asset.coordinate_system = {
        .handedness = Handedness::right,
        .up_axis = Axis::positive_y,
        .forward_axis = Axis::negative_z,
        .unit = "m",
        .meters_per_unit = 1.0,
    };
    spatial_fbx.geometry.bounds = Bounds3d{
        .min = {-1.0, 0.0, -0.5},
        .max = {1.0, 2.0, 0.5},
    };
    auto spatial_glb = thief_glb();
    spatial_glb.asset.coordinate_system = {
        .handedness = Handedness::left,
        .up_axis = Axis::positive_z,
        .forward_axis = Axis::positive_y,
        .unit = "cm",
        .meters_per_unit = 0.01,
    };
    spatial_glb.geometry.bounds = Bounds3d{
        .min = {-100.0, -50.0, 0.0},
        .max = {100.0, 50.0, 200.0},
    };
    const auto spatial_result = unified3d::operations::analysis::compare_analysis_records(
        spatial_fbx,
        spatial_glb
    );
    expect(spatial_result.success(), "spatial metadata comparison must succeed");
    const auto& spatial_compatibility = spatial_result.comparison->compatibility;
    expect(
        spatial_compatibility.levels[7U].status == ComparisonLevelStatus::match,
        "equivalent bounds in different axes and units must spatially match"
    );
    expect_near(
        *spatial_compatibility.levels[7U].score,
        1.0,
        1.0e-15,
        "canonical spatial bounds must produce a perfect score"
    );
    expect(
        spatial_compatibility.classification
            == CompatibilityClassification::spatial_skin_transfer_required,
        "aligned dense geometry and rig donor must request spatial skin transfer"
    );

    const auto reverse = unified3d::operations::analysis::compare_analysis_records(
        thief_glb(),
        thief_fbx()
    );
    expect(reverse.success(), "reverse comparison must succeed");
    expect(
        reverse.comparison->rig_donor_geometry_target_pattern.donor == InputSide::b,
        "reverse comparison must still select FBX as donor"
    );
    expect(
        reverse.comparison->rig_donor_geometry_target_pattern.target == InputSide::a,
        "reverse comparison must still select GLB as target"
    );
}

void failure_propagation_test() {
    auto invalid = thief_fbx();
    invalid.skin.present = false;
    const auto result = unified3d::operations::analysis::compare_analysis_records(
        invalid,
        thief_glb()
    );
    expect(!result.success(), "invalid input must stop the comparison operation");
    expect(!result.comparison.has_value(), "invalid input must not produce comparison facts");
    const bool has_prefixed_error = std::ranges::any_of(
        result.diagnostics,
        [](const unified3d::Diagnostic& diagnostic) {
            return diagnostic.severity == DiagnosticSeverity::error
                && diagnostic.path.starts_with("$.inputs.a.");
        }
    );
    expect(has_prefixed_error, "input validation errors must identify input A");
}

std::shared_ptr<const std::vector<std::byte>> bytes(const std::size_t size) {
    return std::make_shared<const std::vector<std::byte>>(size);
}

template <typename T>
std::shared_ptr<const std::vector<std::byte>> value_bytes(
    const std::initializer_list<T> values
) {
    auto storage = std::make_shared<std::vector<std::byte>>(values.size() * sizeof(T));
    std::memcpy(storage->data(), values.begin(), storage->size());
    return storage;
}

template <typename T>
std::shared_ptr<const std::vector<std::byte>> value_bytes(
    const std::vector<T>& values
) {
    auto storage = std::make_shared<std::vector<std::byte>>(values.size() * sizeof(T));
    if (!values.empty()) std::memcpy(storage->data(), values.data(), storage->size());
    return storage;
}

PrimitiveBuffers fingerprint_primitive(
    const std::initializer_list<float> positions,
    const std::initializer_list<std::uint32_t> indices
) {
    return PrimitiveBuffers{
        .positions = VertexAttributeBuffer{
            .semantic = VertexSemantic::position,
            .view = BufferView{
                .storage = value_bytes(positions),
                .element_count = positions.size() / 3U,
                .component_count = 3U,
                .scalar_type = ScalarType::float32,
            },
        },
        .indices = IndexBuffer{BufferView{
            .storage = value_bytes(indices),
            .element_count = indices.size(),
            .component_count = 1U,
            .scalar_type = ScalarType::uint32,
        }},
    };
}

void canonical_fingerprint_tests() {
    const PrimitiveBuffers first = fingerprint_primitive(
        {
            0.0F, 0.0F, 0.0F,
            1.0F, 0.0F, 0.0F,
            0.0F, 1.0F, 0.0F,
            1.0F, 1.0F, 0.0F,
        },
        {0U, 1U, 2U, 1U, 3U, 2U}
    );
    const PrimitiveBuffers reordered = fingerprint_primitive(
        {
            1.0F, 1.0F, 0.0F,
            0.0F, 1.0F, 0.0F,
            1.0F, 0.0F, 0.0F,
            0.0F, 0.0F, 0.0F,
        },
        {1U, 0U, 2U, 1U, 2U, 3U}
    );
    const PrimitiveBuffers changed = fingerprint_primitive(
        {
            1.0F, 1.1F, 0.0F,
            0.0F, 1.0F, 0.0F,
            1.0F, 0.0F, 0.0F,
            0.0F, 0.0F, 0.0F,
        },
        {1U, 0U, 2U, 1U, 2U, 3U}
    );
    const auto a = unified3d::geometry::fingerprint_triangle_position_soup(
        std::span<const PrimitiveBuffers>{&first, 1U}
    );
    const auto b = unified3d::geometry::fingerprint_triangle_position_soup(
        std::span<const PrimitiveBuffers>{&reordered, 1U}
    );
    const auto c = unified3d::geometry::fingerprint_triangle_position_soup(
        std::span<const PrimitiveBuffers>{&changed, 1U}
    );
    expect(a.success() && b.success() && c.success(), "canonical fingerprints must compute");
    expect(
        a.fingerprint->digest == b.fingerprint->digest,
        "canonical fingerprint must ignore vertex order, triangle order, and winding"
    );
    expect(
        a.fingerprint->digest != c.fingerprint->digest,
        "canonical fingerprint must detect a geometric position change"
    );
}

void geometry_buffer_tests() {
    const VertexAttributeBuffer positions{
        .semantic = VertexSemantic::position,
        .view = BufferView{
            .storage = bytes(24U),
            .element_count = 2U,
            .component_count = 3U,
            .scalar_type = ScalarType::float32,
        },
    };
    const VertexAttributeBuffer joints{
        .semantic = VertexSemantic::joints,
        .view = BufferView{
            .storage = bytes(16U),
            .element_count = 2U,
            .component_count = 4U,
            .scalar_type = ScalarType::uint16,
        },
    };
    const VertexAttributeBuffer weights{
        .semantic = VertexSemantic::weights,
        .view = BufferView{
            .storage = bytes(32U),
            .element_count = 2U,
            .component_count = 4U,
            .scalar_type = ScalarType::float32,
        },
    };
    SkinTransferBuffers transfer{
        .positions = positions,
        .indices = IndexBuffer{
            .view = BufferView{
                .storage = bytes(6U),
                .element_count = 3U,
                .component_count = 1U,
                .scalar_type = ScalarType::uint16,
            },
        },
        .influence_sets = {
            SkinInfluenceSet{.joints = joints, .weights = weights},
            SkinInfluenceSet{.joints = joints, .weights = weights},
        },
        .max_influences = 6U,
    };
    expect(
        unified3d::geometry::validate_skin_transfer_buffers(transfer).valid(),
        "two JOINTS_n/WEIGHTS_n sets must preserve six influences"
    );

    transfer.influence_sets.pop_back();
    const auto insufficient =
        unified3d::geometry::validate_skin_transfer_buffers(transfer);
    expect(
        !insufficient.valid(),
        "one JOINTS_n/WEIGHTS_n set must not claim six influences"
    );

    transfer.influence_sets.push_back(
        SkinInfluenceSet{.joints = joints, .weights = weights}
    );
    transfer.influence_sets[1U].weights.view.storage = bytes(31U);
    const auto truncated = unified3d::geometry::validate_skin_transfer_buffers(transfer);
    expect(!truncated.valid(), "truncated immutable storage must be rejected");
    expect(
        std::ranges::any_of(
            truncated.diagnostics,
            [](const unified3d::Diagnostic& diagnostic) {
                return diagnostic.code == "BUFFER_BOUNDS";
            }
        ),
        "truncated storage must report BUFFER_BOUNDS"
    );
}

void spatial_skin_transfer_tests() {
    const std::vector<float> positions{
        0.0F, 0.0F, 0.0F,
        1.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F,
    };
    const std::vector<std::uint32_t> indices{0U, 1U, 2U};
    const std::vector<std::uint32_t> joints{
        0U, 0U, 0U, 0U,
        1U, 0U, 0U, 0U,
        2U, 0U, 0U, 0U,
    };
    const std::vector<float> weights{
        1.0F, 0.0F, 0.0F, 0.0F,
        1.0F, 0.0F, 0.0F, 0.0F,
        1.0F, 0.0F, 0.0F, 0.0F,
    };
    PrimitiveBuffers source{
        .positions = {VertexSemantic::position, {value_bytes(positions), 0U, 0U, 3U, 3U, ScalarType::float32}},
        .indices = IndexBuffer{{value_bytes(indices), 0U, 0U, 3U, 1U, ScalarType::uint32}},
        .influence_sets = {{
            .joints = {VertexSemantic::joints, {value_bytes(joints), 0U, 0U, 3U, 4U, ScalarType::uint32}},
            .weights = {VertexSemantic::weights, {value_bytes(weights), 0U, 0U, 3U, 4U, ScalarType::float32}},
        }},
        .max_influences = 1U,
    };
    const std::vector<float> target_positions{1.0F / 3.0F, 1.0F / 3.0F, 0.01F};
    PrimitiveBuffers target{
        .positions = {VertexSemantic::position, {value_bytes(target_positions), 0U, 0U, 1U, 3U, ScalarType::float32}},
    };
    const std::array source_inputs{
        unified3d::geometry::SpatialPrimitiveInput{.buffers = &source},
    };
    const std::array target_inputs{
        unified3d::geometry::SpatialPrimitiveInput{.buffers = &target},
    };
    const auto transferred = unified3d::geometry::transfer_skin_weights_spatially(
        source_inputs,
        target_inputs,
        {.quality = unified3d::geometry::SpatialTransferQuality::diagnostic,
         .maximum_influences = 4U,
         .minimum_weight = 1.0e-8,
         .maximum_distance_m = 0.02}
    );
    expect(transferred.success(), "spatial barycentric skin transfer must succeed");
    expect(transferred.report->matched_vertex_count == 1U, "target vertex must match donor triangle");
    expect_near(transferred.report->maximum_distance_m, 0.01, 1.0e-6, "surface distance must be measured in world meters");
    expect(transferred.primitives[0].max_influences == 3U, "three barycentric corner joints must survive");
    const auto& output = transferred.primitives[0].influence_sets[0];
    std::array<float, 4> output_weights{};
    std::memcpy(output_weights.data(), output.weights.view.storage->data(), sizeof(output_weights));
    expect_near(output_weights[0], 1.0 / 3.0, 1.0e-6, "first interpolated weight must normalize");
    expect_near(output_weights[1], 1.0 / 3.0, 1.0e-6, "second interpolated weight must normalize");
    expect_near(output_weights[2], 1.0 / 3.0, 1.0e-6, "third interpolated weight must normalize");

    const auto rejected = unified3d::geometry::transfer_skin_weights_spatially(
        source_inputs,
        target_inputs,
        {.maximum_distance_m = 0.001}
    );
    expect(!rejected.success(), "distance policy must reject unrelated target geometry");
}

}  // namespace

int main() {
    validation_tests();
    comparison_parity_tests();
    failure_propagation_test();
    geometry_buffer_tests();
    canonical_fingerprint_tests();
    spatial_skin_transfer_tests();

    if (failures != 0) {
        std::cerr << failures << " native test(s) failed.\n";
        return EXIT_FAILURE;
    }
    std::cout << "All Unified3D native tests passed.\n";
    return EXIT_SUCCESS;
}
