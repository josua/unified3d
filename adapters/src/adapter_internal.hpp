#pragma once

#include <cstring>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <unified3d/adapters/asset_format_adapter.hpp>

namespace unified3d::adapters::detail {

template <typename T>
geometry::ImmutableBytes immutable_bytes(std::vector<T> values) {
    static_assert(std::is_trivially_copyable_v<T>);
    auto bytes = std::make_shared<std::vector<std::byte>>(values.size() * sizeof(T));
    if (!values.empty()) {
        std::memcpy(bytes->data(), values.data(), bytes->size());
    }
    return bytes;
}

inline Diagnostic error(
    std::string code,
    std::string message,
    std::string path = "$"
) {
    return Diagnostic{
        .severity = DiagnosticSeverity::error,
        .code = std::move(code),
        .message = std::move(message),
        .path = std::move(path),
    };
}

[[nodiscard]] DecodeResult decode_cgltf(const std::filesystem::path& path);
[[nodiscard]] DecodeResult decode_ufbx(const std::filesystem::path& path);
#if defined(UNIFIED3D_HAS_AUTODESK_FBX)
[[nodiscard]] DecodeResult decode_autodesk_fbx(const std::filesystem::path& path);
#endif

}  // namespace unified3d::adapters::detail
