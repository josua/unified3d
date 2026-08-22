#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <unified3d/core/diagnostic.hpp>

namespace unified3d::runtime {

struct GlbSpatialNormalizationOptions {
    std::optional<double> expected_position_height_m{1.70};
    double height_tolerance_m{0.05};
    bool correct_scale_factor{true};
    bool remove_emissive_channel{true};
    bool remove_head_helper_bones{false};
    bool remove_animations{false};
    bool overwrite{false};
};

struct GlbSpatialNormalizationReport {
    std::filesystem::path source_path;
    std::filesystem::path output_path;
    std::uint64_t source_size_bytes{};
    std::uint64_t output_size_bytes{};
    std::uint64_t root_node_index{};
    std::string root_node_name;
    double absorbed_uniform_scale{};
    double position_height_m{};
    std::uint64_t modified_node_translation_count{};
    std::uint64_t modified_animation_accessor_count{};
    std::uint64_t modified_inverse_bind_matrix_count{};
    std::uint64_t removed_emissive_texture_count{};
    std::uint64_t zeroed_emissive_factor_count{};
    std::uint64_t removed_head_helper_node_count{};
    std::uint64_t removed_head_helper_joint_count{};
    std::uint64_t removed_head_helper_animation_channel_count{};
    std::uint64_t removed_animation_clip_count{};
    std::uint64_t removed_animation_channel_count{};
    std::uint64_t removed_animation_sampler_count{};
    bool scale_correction_applied{};
    bool emissive_correction_applied{};
    bool head_helper_bone_removal_applied{};
    bool animation_removal_applied{};
};

struct GlbSpatialNormalizationResult {
    std::optional<GlbSpatialNormalizationReport> report;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool success() const noexcept;
};

// Normalizes the mixed-unit pattern emitted by Blender/Meshy exports where
// mesh POSITION data is already metric, while a uniformly-scaled root keeps
// skeleton translations, animation translations and inverse bind matrices in
// a legacy unit domain. The operation is deliberately guarded and refuses
// ambiguous hierarchies instead of guessing. Scale and Meshy emission
// corrections are independently selectable. Emission correction removes
// emissiveTexture references and writes an explicit emissiveFactor of
// [0, 0, 0] on every material. Image and texture resources remain untouched
// because they may also be referenced by baseColorTexture. Optional Meshy
// cleanup removes the zero-weight head_end/headfront helper joints and their
// animation channels. Animation removal erases every clip while preserving
// the skinned mesh, joint hierarchy, skin weights and inverse bind matrices.
[[nodiscard]] GlbSpatialNormalizationResult normalize_mixed_unit_rig_glb(
    const std::filesystem::path& input_path,
    const std::filesystem::path& output_path,
    const GlbSpatialNormalizationOptions& options = {}
);

}  // namespace unified3d::runtime
