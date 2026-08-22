#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <unified3d/core/diagnostic.hpp>
#include <unified3d/core/geometry/buffers.hpp>

namespace unified3d::geometry {

using Matrix4d = std::array<double, 16>;

struct SpatialPrimitiveInput {
    const PrimitiveBuffers* buffers{};
    Matrix4d local_to_world{
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, 0.0, 0.0, 1.0,
    };
};

enum class SpatialTransferQuality {
    fast,
    balanced,
    precise,
    diagnostic,
};

struct SpatialSkinTransferOptions {
    SpatialTransferQuality quality{SpatialTransferQuality::balanced};
    std::uint32_t maximum_influences{4U};
    double minimum_weight{1.0e-6};
    std::optional<double> maximum_distance_m;
};

struct SpatialMappingSample {
    std::uint64_t target_vertex{};
    std::uint64_t source_triangle{};
    std::array<double, 3> barycentric{};
    double distance_m{};
};

struct TransferredPrimitiveSkin {
    std::vector<SkinInfluenceSet> influence_sets;
    std::uint32_t max_influences{};
    std::uint64_t vertex_count{};
};

struct SpatialSkinTransferReport {
    std::uint64_t source_triangle_count{};
    std::uint64_t target_vertex_count{};
    std::uint64_t matched_vertex_count{};
    std::uint64_t rejected_vertex_count{};
    double mean_distance_m{};
    double maximum_distance_m{};
    std::uint32_t output_max_influences{};
    std::vector<SpatialMappingSample> diagnostic_samples;
};

struct SpatialSkinTransferResult {
    std::vector<TransferredPrimitiveSkin> primitives;
    std::optional<SpatialSkinTransferReport> report;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool success() const noexcept;
};

[[nodiscard]] SpatialSkinTransferResult transfer_skin_weights_spatially(
    std::span<const SpatialPrimitiveInput> source,
    std::span<const SpatialPrimitiveInput> target,
    const SpatialSkinTransferOptions& options = {}
);

}  // namespace unified3d::geometry
