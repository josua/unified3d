#include <unified3d/adapters/asset_format_adapter.hpp>

#include "adapter_internal.hpp"

#include <algorithm>
#include <cctype>

namespace unified3d::adapters {

bool DecodeResult::success() const noexcept {
    return asset.has_value() && std::ranges::none_of(diagnostics, [](const Diagnostic& item) {
        return item.severity == DiagnosticSeverity::error;
    });
}

DecodeResult decode_asset_buffers(
    const std::filesystem::path& path,
    const AdapterBackend backend
) {
    const AdapterBackend selected = resolve_adapter_backend(path, backend);

    std::string extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](const unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });

    if (selected == AdapterBackend::cgltf && (extension == ".glb" || extension == ".gltf")) {
        return detail::decode_cgltf(path);
    }
    if (selected == AdapterBackend::ufbx && extension == ".fbx") {
        return detail::decode_ufbx(path);
    }
#if defined(UNIFIED3D_HAS_AUTODESK_FBX)
    if (selected == AdapterBackend::autodesk_fbx && extension == ".fbx") {
        return detail::decode_autodesk_fbx(path);
    }
#endif
    return {
        .asset = std::nullopt,
        .diagnostics = {detail::error(
            "ADAPTER_FORMAT_MISMATCH",
            "The requested native adapter does not support this file extension."
        )},
    };
}

AdapterBackend resolve_adapter_backend(
    const std::filesystem::path& path,
    const AdapterBackend requested
) noexcept {
    if (requested != AdapterBackend::automatic) {
        return requested;
    }
    std::string extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](const unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });

    if (extension == ".glb" || extension == ".gltf") {
        return AdapterBackend::cgltf;
    }
    if (extension == ".fbx") {
#if defined(UNIFIED3D_HAS_AUTODESK_FBX)
        return AdapterBackend::autodesk_fbx;
#else
        return AdapterBackend::ufbx;
#endif
    }
    return AdapterBackend::automatic;
}

std::string_view adapter_backend_name(const AdapterBackend backend) noexcept {
    switch (backend) {
        case AdapterBackend::automatic: return "auto";
        case AdapterBackend::cgltf: return "cgltf";
        case AdapterBackend::ufbx: return "ufbx";
        case AdapterBackend::autodesk_fbx: return "autodesk_fbx";
    }
    return "unknown";
}

std::string_view geometry_domain_name(const GeometryDomain domain) noexcept {
    switch (domain) {
        case GeometryDomain::geometric_vertices: return "GEOMETRIC_VERTICES";
        case GeometryDomain::render_vertices: return "RENDER_VERTICES";
    }
    return "UNKNOWN";
}

bool autodesk_fbx_available() noexcept {
#if defined(UNIFIED3D_HAS_AUTODESK_FBX)
    return true;
#else
    return false;
#endif
}

}  // namespace unified3d::adapters
