#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <unified3d/core/diagnostic.hpp>
#include <unified3d/core/geometry/buffers.hpp>

namespace unified3d::adapters {

enum class AdapterBackend {
    automatic,
    cgltf,
    ufbx,
    autodesk_fbx,
};

enum class GeometryDomain {
    geometric_vertices,
    render_vertices,
};

using Matrix4d = std::array<double, 16>;

struct DecodedPrimitive {
    std::string name;
    std::uint64_t source_mesh_index{};
    std::uint64_t source_primitive_index{};
    GeometryDomain domain{GeometryDomain::render_vertices};
    Matrix4d local_to_world{
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, 0.0, 0.0, 1.0,
    };
    geometry::PrimitiveBuffers buffers;
};

struct DecodedAssetBuffers {
    std::string adapter;
    std::string coordinate_system{"RIGHT_HANDED_Y_UP"};
    double unit_meters{1.0};
    std::vector<std::string> joint_names;
    std::vector<DecodedPrimitive> primitives;
};

struct DecodeResult {
    std::optional<DecodedAssetBuffers> asset;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool success() const noexcept;
};

[[nodiscard]] DecodeResult decode_asset_buffers(
    const std::filesystem::path& path,
    AdapterBackend backend = AdapterBackend::automatic
);

[[nodiscard]] AdapterBackend resolve_adapter_backend(
    const std::filesystem::path& path,
    AdapterBackend requested = AdapterBackend::automatic
) noexcept;
[[nodiscard]] std::string_view adapter_backend_name(AdapterBackend backend) noexcept;
[[nodiscard]] std::string_view geometry_domain_name(GeometryDomain domain) noexcept;
[[nodiscard]] bool autodesk_fbx_available() noexcept;

}  // namespace unified3d::adapters
