#include <unified3d/core/analysis/analysis_record.hpp>
#include <unified3d/operations/analysis/compare_analysis_records.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using unified3d::DiagnosticSeverity;
using unified3d::analysis::AnalysisRecord;
using unified3d::analysis::AssetContainer;
using unified3d::analysis::AssetFormat;
using unified3d::analysis::GeometricVertexSemantic;
using unified3d::analysis::TopologySignature;
using unified3d::operations::analysis::CompatibilityClassification;
using unified3d::operations::analysis::ComparisonLevelStatus;
using unified3d::operations::analysis::InputSide;

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
    expect(compatibility.levels.size() == 7U, "compatibility must contain levels zero through six");
    expect(
        compatibility.classification
            == CompatibilityClassification::advanced_transfer_required,
        "thief transfer must require advanced mapping"
    );
    expect(compatibility.recommended_next_level == 1U, "coordinate system is the next unresolved level");
    expect_near(compatibility.coverage, 2.0 / 6.0, 1.0e-15, "coverage must match Python");
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

}  // namespace

int main() {
    validation_tests();
    comparison_parity_tests();
    failure_propagation_test();

    if (failures != 0) {
        std::cerr << failures << " native test(s) failed.\n";
        return EXIT_FAILURE;
    }
    std::cout << "All Unified3D native tests passed.\n";
    return EXIT_SUCCESS;
}
