#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <unified3d/core/diagnostic.hpp>
#include <unified3d/core/geometry/buffers.hpp>

namespace unified3d::geometry {

using BindMatrix4d = std::array<double, 16>;

[[nodiscard]] constexpr BindMatrix4d identity_bind_matrix() noexcept {
    return {
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, 0.0, 0.0, 1.0,
    };
}

struct SkeletonJoint {
    std::string name;
    std::optional<std::uint32_t> parent_index;
    BindMatrix4d local_bind_transform{identity_bind_matrix()};
    BindMatrix4d inverse_bind_matrix{identity_bind_matrix()};
};

struct SkeletonDefinition {
    std::vector<SkeletonJoint> joints;
};

// Removes a source joint from the exported palette. Its weights are transferred
// either to target_joint_index or, when omitted, to its nearest retained ancestor.
struct JointCollapse {
    std::uint32_t joint_index{};
    std::optional<std::uint32_t> target_joint_index;
};

// Corrects a retained joint without guessing matrix conventions. Both matrices
// are supplied together so the caller can preserve the bind-pose invariant.
struct JointRelocation {
    std::uint32_t joint_index{};
    BindMatrix4d corrected_local_bind_transform{identity_bind_matrix()};
    BindMatrix4d corrected_inverse_bind_matrix{identity_bind_matrix()};
};

struct SkeletonRepairOptions {
    std::vector<JointCollapse> collapsed_joints;
    std::vector<JointRelocation> relocated_joints;
    std::uint32_t maximum_influences{4U};
    double minimum_weight{1.0e-8};
    std::optional<std::uint32_t> fallback_joint_index;
};

struct SkeletonRepairResult {
    std::optional<SkeletonDefinition> skeleton;
    std::vector<PrimitiveBuffers> primitives;
    // One entry per source joint. Collapsed joints point to their repaired target.
    std::vector<std::optional<std::uint32_t>> source_to_repaired_joint;
    std::uint64_t collapsed_joint_count{};
    std::uint64_t relocated_joint_count{};
    std::uint64_t remapped_influence_count{};
    std::uint64_t merged_influence_count{};
    std::uint64_t zero_weight_vertex_count{};
    bool requires_animation_retargeting{};
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool success() const noexcept;
};

[[nodiscard]] SkeletonRepairResult repair_skeleton_and_skin(
    const SkeletonDefinition& source_skeleton,
    std::span<const PrimitiveBuffers> source_primitives,
    const SkeletonRepairOptions& options = {}
);

}  // namespace unified3d::geometry
