#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

#include <unified3d/core/diagnostic.hpp>

namespace unified3d::adapters {

struct GlbToFbxConversionOptions {
    bool embed_media{true};
    bool overwrite{false};
};

struct GlbToFbxConversionReport {
    std::filesystem::path source_path;
    std::filesystem::path output_path;
    std::uint64_t source_size_bytes{};
    std::uint64_t output_size_bytes{};
    std::uint64_t mesh_count{};
    std::uint64_t primitive_count{};
    std::uint64_t control_point_count{};
    std::uint64_t triangle_count{};
    std::uint64_t material_count{};
    std::uint64_t texture_count{};
    std::uint64_t embedded_media_count{};
    bool geometry_preserved{};
    bool media_embedded{};
};

struct GlbToFbxConversionResult {
    std::optional<GlbToFbxConversionReport> report;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool success() const noexcept;
};

[[nodiscard]] GlbToFbxConversionResult convert_unrigged_glb_to_fbx(
    const std::filesystem::path& source_path,
    const std::filesystem::path& output_path,
    const GlbToFbxConversionOptions& options = {}
);

}  // namespace unified3d::adapters
