#include <unified3d/runtime/glb_spatial_normalizer.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace unified3d::runtime {
namespace {

using Json = nlohmann::json;

constexpr std::uint32_t glb_magic = 0x46546c67U;
constexpr std::uint32_t glb_version = 2U;
constexpr std::uint32_t json_chunk_type = 0x4e4f534aU;
constexpr std::uint32_t bin_chunk_type = 0x004e4942U;
constexpr std::uint32_t float_component_type = 5126U;
constexpr std::uint32_t unsigned_byte_component_type = 5121U;
constexpr std::uint32_t unsigned_short_component_type = 5123U;
constexpr std::uint32_t unsigned_int_component_type = 5125U;

struct GlbDocument {
    Json json;
    std::vector<std::byte> binary;
};

struct AccessorView {
    std::size_t offset{};
    std::size_t stride{};
    std::size_t count{};
    std::size_t components{};
};

struct EmissiveCorrectionCounts {
    std::uint64_t removed_texture_count{};
    std::uint64_t zeroed_factor_count{};
};

struct RawAccessorView {
    std::size_t offset{};
    std::size_t stride{};
    std::size_t count{};
    std::size_t components{};
    std::size_t component_size{};
    std::uint32_t component_type{};
    bool normalized{};
};

struct HeadHelperRemovalCounts {
    std::uint64_t removed_node_count{};
    std::uint64_t removed_joint_count{};
    std::uint64_t removed_animation_channel_count{};
};

struct AnimationRemovalCounts {
    std::uint64_t removed_clip_count{};
    std::uint64_t removed_channel_count{};
    std::uint64_t removed_sampler_count{};
};

void add_error(
    GlbSpatialNormalizationResult& result,
    std::string code,
    std::string message,
    std::string path
);

bool is_zero_emissive_factor(const Json& value) {
    if (!value.is_array() || value.size() != 3U) return false;
    return std::ranges::all_of(value, [](const Json& component) {
        return component.is_number()
            && std::isfinite(component.get<double>())
            && component.get<double>() == 0.0;
    });
}

std::optional<EmissiveCorrectionCounts> disable_material_emission(
    Json& root,
    GlbSpatialNormalizationResult& result
) {
    EmissiveCorrectionCounts counts;
    if (!root.contains("materials")) return counts;
    if (!root["materials"].is_array()) {
        add_error(result, "GLB_MATERIALS", "Materials must be an array.", "$.materials");
        return std::nullopt;
    }
    for (std::size_t index = 0U; index < root["materials"].size(); ++index) {
        Json& material = root["materials"][index];
        if (!material.is_object()) {
            add_error(
                result,
                "GLB_MATERIAL",
                "Each material must be an object.",
                "$.materials[" + std::to_string(index) + "]"
            );
            return std::nullopt;
        }
        counts.removed_texture_count += material.erase("emissiveTexture");
        if (!material.contains("emissiveFactor")
            || !is_zero_emissive_factor(material["emissiveFactor"])) {
            ++counts.zeroed_factor_count;
        }
        material["emissiveFactor"] = Json::array({0.0, 0.0, 0.0});
    }
    return counts;
}

void add_error(
    GlbSpatialNormalizationResult& result,
    std::string code,
    std::string message,
    std::string path = "$"
) {
    result.diagnostics.push_back(Diagnostic{
        .severity = DiagnosticSeverity::error,
        .code = std::move(code),
        .message = std::move(message),
        .path = std::move(path),
    });
}

std::optional<std::uint32_t> read_u32(
    const std::vector<std::byte>& bytes,
    const std::size_t offset
) {
    if (offset > bytes.size() || bytes.size() - offset < 4U) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(bytes[offset])
        | (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U)
        | (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U)
        | (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

void append_u32(std::vector<std::byte>& bytes, const std::uint32_t value) {
    bytes.push_back(static_cast<std::byte>(value & 0xffU));
    bytes.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
    bytes.push_back(static_cast<std::byte>((value >> 16U) & 0xffU));
    bytes.push_back(static_cast<std::byte>((value >> 24U) & 0xffU));
}

std::optional<std::vector<std::byte>> read_file(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary | std::ios::ate};
    if (!input) {
        return std::nullopt;
    }
    const std::streampos end = input.tellg();
    if (end < 0 || static_cast<std::uint64_t>(end)
        > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return std::nullopt;
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(end));
    input.seekg(0, std::ios::beg);
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    return input ? std::optional<std::vector<std::byte>>{std::move(bytes)} : std::nullopt;
}

std::optional<GlbDocument> decode_glb(
    const std::vector<std::byte>& bytes,
    GlbSpatialNormalizationResult& result
) {
    if (bytes.size() < 20U || read_u32(bytes, 0U) != glb_magic
        || read_u32(bytes, 4U) != glb_version
        || read_u32(bytes, 8U) != bytes.size()) {
        add_error(result, "GLB_HEADER", "Input is not a well-formed glTF 2.0 binary container.");
        return std::nullopt;
    }
    std::optional<std::string> json_text;
    std::optional<std::vector<std::byte>> binary;
    std::size_t offset = 12U;
    while (offset < bytes.size()) {
        const auto chunk_length = read_u32(bytes, offset);
        const auto chunk_type = read_u32(bytes, offset + 4U);
        if (!chunk_length.has_value() || !chunk_type.has_value()
            || offset + 8U > bytes.size()
            || *chunk_length > bytes.size() - offset - 8U) {
            add_error(result, "GLB_CHUNK", "GLB chunk table is truncated or invalid.");
            return std::nullopt;
        }
        const std::size_t data_offset = offset + 8U;
        if (*chunk_type == json_chunk_type) {
            if (json_text.has_value()) {
                add_error(result, "GLB_JSON_CHUNK", "GLB contains more than one JSON chunk.");
                return std::nullopt;
            }
            json_text = std::string{
                reinterpret_cast<const char*>(bytes.data() + data_offset),
                *chunk_length,
            };
            while (!json_text->empty()
                && (json_text->back() == ' ' || json_text->back() == '\0')) {
                json_text->pop_back();
            }
        } else if (*chunk_type == bin_chunk_type) {
            if (binary.has_value()) {
                add_error(result, "GLB_BIN_CHUNK", "GLB contains more than one BIN chunk.");
                return std::nullopt;
            }
            binary = std::vector<std::byte>(
                bytes.begin() + static_cast<std::ptrdiff_t>(data_offset),
                bytes.begin() + static_cast<std::ptrdiff_t>(data_offset + *chunk_length)
            );
        } else {
            add_error(result, "GLB_UNKNOWN_CHUNK", "Unsupported additional GLB chunk encountered.");
            return std::nullopt;
        }
        offset = data_offset + *chunk_length;
    }
    if (!json_text.has_value() || !binary.has_value()) {
        add_error(result, "GLB_CHUNKS", "A self-contained GLB requires one JSON and one BIN chunk.");
        return std::nullopt;
    }
    try {
        return GlbDocument{Json::parse(*json_text), std::move(*binary)};
    } catch (const Json::exception& error) {
        add_error(result, "GLB_JSON", std::string{"Cannot parse the GLB JSON chunk: "} + error.what());
        return std::nullopt;
    }
}

std::optional<std::size_t> component_count(const std::string& type) {
    if (type == "SCALAR") return 1U;
    if (type == "VEC2") return 2U;
    if (type == "VEC3") return 3U;
    if (type == "VEC4") return 4U;
    if (type == "MAT4") return 16U;
    return std::nullopt;
}

std::optional<AccessorView> accessor_view(
    const Json& root,
    const std::size_t accessor_index,
    const std::vector<std::byte>& binary,
    GlbSpatialNormalizationResult& result,
    const std::string& path
) {
    if (!root.contains("accessors") || !root["accessors"].is_array()
        || accessor_index >= root["accessors"].size()) {
        add_error(result, "GLB_ACCESSOR", "Accessor index is outside the accessor table.", path);
        return std::nullopt;
    }
    const Json& accessor = root["accessors"][accessor_index];
    if (!accessor.is_object() || accessor.contains("sparse")
        || !accessor.contains("bufferView") || !accessor["bufferView"].is_number_unsigned()
        || !accessor.contains("componentType") || !accessor["componentType"].is_number_unsigned()
        || accessor["componentType"].get<std::uint32_t>() != float_component_type
        || !accessor.contains("count") || !accessor["count"].is_number_unsigned()
        || !accessor.contains("type") || !accessor["type"].is_string()) {
        add_error(result, "GLB_ACCESSOR_LAYOUT", "A non-sparse FLOAT accessor is required.", path);
        return std::nullopt;
    }
    const std::size_t buffer_view_index = accessor["bufferView"].get<std::size_t>();
    if (!root.contains("bufferViews") || !root["bufferViews"].is_array()
        || buffer_view_index >= root["bufferViews"].size()) {
        add_error(result, "GLB_BUFFER_VIEW", "Accessor references an invalid buffer view.", path);
        return std::nullopt;
    }
    const Json& view = root["bufferViews"][buffer_view_index];
    if (!view.is_object() || !view.contains("buffer") || view["buffer"] != 0
        || !view.contains("byteLength") || !view["byteLength"].is_number_unsigned()) {
        add_error(result, "GLB_BUFFER_VIEW_LAYOUT", "Accessor must reference buffer zero in the GLB BIN chunk.", path);
        return std::nullopt;
    }
    const auto components = component_count(accessor["type"].get<std::string>());
    if (!components.has_value()) {
        add_error(result, "GLB_ACCESSOR_TYPE", "Unsupported accessor type.", path);
        return std::nullopt;
    }
    const std::size_t element_size = *components * sizeof(float);
    const std::size_t stride = view.value("byteStride", element_size);
    const std::size_t view_offset = view.value("byteOffset", 0U);
    const std::size_t local_offset = accessor.value("byteOffset", 0U);
    const std::size_t count = accessor["count"].get<std::size_t>();
    if (stride < element_size || view_offset > binary.size()
        || local_offset > binary.size() - view_offset) {
        add_error(result, "GLB_ACCESSOR_BOUNDS", "Accessor offset or stride is invalid.", path);
        return std::nullopt;
    }
    const std::size_t offset = view_offset + local_offset;
    if (count > 0U && (count - 1U > (std::numeric_limits<std::size_t>::max() - offset) / stride
        || offset + (count - 1U) * stride > binary.size()
        || binary.size() - (offset + (count - 1U) * stride) < element_size)) {
        add_error(result, "GLB_ACCESSOR_BOUNDS", "Accessor exceeds the GLB BIN chunk.", path);
        return std::nullopt;
    }
    return AccessorView{offset, stride, count, *components};
}

std::optional<std::size_t> component_size(const std::uint32_t component_type) {
    if (component_type == unsigned_byte_component_type) return 1U;
    if (component_type == unsigned_short_component_type) return 2U;
    if (component_type == unsigned_int_component_type
        || component_type == float_component_type) return 4U;
    return std::nullopt;
}

std::optional<RawAccessorView> raw_accessor_view(
    const Json& root,
    const std::size_t accessor_index,
    const std::vector<std::byte>& binary,
    GlbSpatialNormalizationResult& result,
    const std::string& path
) {
    if (!root.contains("accessors") || !root["accessors"].is_array()
        || accessor_index >= root["accessors"].size()) {
        add_error(result, "GLB_ACCESSOR", "Accessor index is outside the accessor table.", path);
        return std::nullopt;
    }
    const Json& accessor = root["accessors"][accessor_index];
    if (!accessor.is_object() || accessor.contains("sparse")
        || !accessor.contains("bufferView") || !accessor["bufferView"].is_number_unsigned()
        || !accessor.contains("componentType") || !accessor["componentType"].is_number_unsigned()
        || !accessor.contains("count") || !accessor["count"].is_number_unsigned()
        || !accessor.contains("type") || !accessor["type"].is_string()) {
        add_error(result, "GLB_ACCESSOR_LAYOUT", "A non-sparse accessor is required.", path);
        return std::nullopt;
    }
    const auto components = component_count(accessor["type"].get<std::string>());
    const std::uint32_t type = accessor["componentType"].get<std::uint32_t>();
    const auto size = component_size(type);
    if (!components.has_value() || !size.has_value()) {
        add_error(result, "GLB_ACCESSOR_TYPE", "Unsupported accessor component or element type.", path);
        return std::nullopt;
    }
    const std::size_t view_index = accessor["bufferView"].get<std::size_t>();
    if (!root.contains("bufferViews") || !root["bufferViews"].is_array()
        || view_index >= root["bufferViews"].size()) {
        add_error(result, "GLB_BUFFER_VIEW", "Accessor references an invalid buffer view.", path);
        return std::nullopt;
    }
    const Json& view = root["bufferViews"][view_index];
    if (!view.is_object() || !view.contains("buffer") || view["buffer"] != 0
        || !view.contains("byteLength") || !view["byteLength"].is_number_unsigned()) {
        add_error(result, "GLB_BUFFER_VIEW_LAYOUT", "Accessor must reference buffer zero.", path);
        return std::nullopt;
    }
    const std::size_t element_size = *components * *size;
    const std::size_t stride = view.value("byteStride", element_size);
    const std::size_t view_offset = view.value("byteOffset", 0U);
    const std::size_t local_offset = accessor.value("byteOffset", 0U);
    const std::size_t count = accessor["count"].get<std::size_t>();
    if (stride < element_size || view_offset > binary.size()
        || local_offset > binary.size() - view_offset) {
        add_error(result, "GLB_ACCESSOR_BOUNDS", "Accessor offset or stride is invalid.", path);
        return std::nullopt;
    }
    const std::size_t offset = view_offset + local_offset;
    if (count > 0U && (count - 1U > (std::numeric_limits<std::size_t>::max() - offset) / stride
        || offset + (count - 1U) * stride > binary.size()
        || binary.size() - (offset + (count - 1U) * stride) < element_size)) {
        add_error(result, "GLB_ACCESSOR_BOUNDS", "Accessor exceeds the GLB BIN chunk.", path);
        return std::nullopt;
    }
    return RawAccessorView{
        offset,
        stride,
        count,
        *components,
        *size,
        type,
        accessor.value("normalized", false),
    };
}

std::optional<std::uint32_t> read_unsigned_component(
    const std::vector<std::byte>& binary,
    const RawAccessorView& view,
    const std::size_t element,
    const std::size_t component
) {
    if (element >= view.count || component >= view.components) return std::nullopt;
    const std::size_t offset = view.offset + element * view.stride
        + component * view.component_size;
    if (view.component_type == unsigned_byte_component_type) {
        return static_cast<std::uint32_t>(binary[offset]);
    }
    if (view.component_type == unsigned_short_component_type) {
        std::uint16_t value{};
        std::memcpy(&value, binary.data() + offset, sizeof(value));
        return value;
    }
    if (view.component_type == unsigned_int_component_type) {
        std::uint32_t value{};
        std::memcpy(&value, binary.data() + offset, sizeof(value));
        return value;
    }
    return std::nullopt;
}

bool write_unsigned_component(
    std::vector<std::byte>& binary,
    const RawAccessorView& view,
    const std::size_t element,
    const std::size_t component,
    const std::uint32_t value
) {
    if (element >= view.count || component >= view.components) return false;
    const std::size_t offset = view.offset + element * view.stride
        + component * view.component_size;
    if (view.component_type == unsigned_byte_component_type
        && value <= std::numeric_limits<std::uint8_t>::max()) {
        binary[offset] = static_cast<std::byte>(value);
        return true;
    }
    if (view.component_type == unsigned_short_component_type
        && value <= std::numeric_limits<std::uint16_t>::max()) {
        const auto encoded = static_cast<std::uint16_t>(value);
        std::memcpy(binary.data() + offset, &encoded, sizeof(encoded));
        return true;
    }
    if (view.component_type == unsigned_int_component_type) {
        std::memcpy(binary.data() + offset, &value, sizeof(value));
        return true;
    }
    return false;
}

std::optional<double> read_weight_component(
    const std::vector<std::byte>& binary,
    const RawAccessorView& view,
    const std::size_t element,
    const std::size_t component
) {
    if (element >= view.count || component >= view.components) return std::nullopt;
    const std::size_t offset = view.offset + element * view.stride
        + component * view.component_size;
    if (view.component_type == float_component_type) {
        float value{};
        std::memcpy(&value, binary.data() + offset, sizeof(value));
        return std::isfinite(value) ? std::optional<double>{value} : std::nullopt;
    }
    const auto value = read_unsigned_component(binary, view, element, component);
    if (!value.has_value() || !view.normalized) return std::nullopt;
    if (view.component_type == unsigned_byte_component_type) {
        return static_cast<double>(*value) / 255.0;
    }
    if (view.component_type == unsigned_short_component_type) {
        return static_cast<double>(*value) / 65535.0;
    }
    return std::nullopt;
}

std::string ascii_lower(std::string value) {
    std::ranges::transform(value, value.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::optional<float> read_float(const std::vector<std::byte>& bytes, const std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < sizeof(float)) {
        return std::nullopt;
    }
    float value{};
    std::memcpy(&value, bytes.data() + offset, sizeof(float));
    return value;
}

bool scale_accessor_components(
    std::vector<std::byte>& binary,
    const AccessorView& view,
    const double factor,
    const std::set<std::size_t>& components
) {
    for (std::size_t element = 0U; element < view.count; ++element) {
        for (const std::size_t component : components) {
            if (component >= view.components) {
                return false;
            }
            const std::size_t offset = view.offset + element * view.stride
                + component * sizeof(float);
            const auto source = read_float(binary, offset);
            if (!source.has_value() || !std::isfinite(*source)) {
                return false;
            }
            const double scaled = static_cast<double>(*source) * factor;
            if (!std::isfinite(scaled)
                || std::abs(scaled) > static_cast<double>(std::numeric_limits<float>::max())) {
                return false;
            }
            const float target = static_cast<float>(scaled);
            std::memcpy(binary.data() + offset, &target, sizeof(float));
        }
    }
    return true;
}

std::optional<HeadHelperRemovalCounts> remove_meshy_head_helpers(
    GlbDocument& document,
    GlbSpatialNormalizationResult& result
) {
    Json& root = document.json;
    HeadHelperRemovalCounts counts;
    Json& nodes = root["nodes"];
    std::set<std::size_t> removed_nodes;
    for (std::size_t index = 0U; index < nodes.size(); ++index) {
        if (!nodes[index].is_object()) continue;
        const std::string name = ascii_lower(nodes[index].value("name", std::string{}));
        if (name == "head_end" || name == "headfront") {
            removed_nodes.insert(index);
        }
    }
    if (removed_nodes.empty()) return counts;
    if (!root.contains("skins") || !root["skins"].is_array()) {
        add_error(result, "GLB_SKINS", "Head helper removal requires a skin table.", "$.skins");
        return std::nullopt;
    }

    std::map<std::size_t, std::vector<std::size_t>> accessor_remaps;
    std::set<std::size_t> compacted_inverse_bind_accessors;
    for (std::size_t skin_index = 0U; skin_index < root["skins"].size(); ++skin_index) {
        Json& skin = root["skins"][skin_index];
        if (!skin.is_object() || !skin.contains("joints") || !skin["joints"].is_array()) {
            add_error(result, "GLB_SKIN_JOINTS", "Each skin must contain a joint array.", "$.skins");
            return std::nullopt;
        }
        const Json old_joints = skin["joints"];
        std::set<std::size_t> removed_ordinals;
        std::optional<std::size_t> head_ordinal;
        for (std::size_t ordinal = 0U; ordinal < old_joints.size(); ++ordinal) {
            if (!old_joints[ordinal].is_number_unsigned()) {
                add_error(result, "GLB_SKIN_JOINT", "Skin joint index must be unsigned.", "$.skins[].joints");
                return std::nullopt;
            }
            const std::size_t node_index = old_joints[ordinal].get<std::size_t>();
            if (node_index >= nodes.size()) {
                add_error(result, "GLB_SKIN_JOINT", "Skin references an invalid node.", "$.skins[].joints");
                return std::nullopt;
            }
            if (removed_nodes.contains(node_index)) removed_ordinals.insert(ordinal);
            if (ascii_lower(nodes[node_index].value("name", std::string{})) == "head") {
                head_ordinal = ordinal;
            }
        }
        if (removed_ordinals.empty()) continue;
        if (!head_ordinal.has_value() || removed_ordinals.contains(*head_ordinal)) {
            add_error(result, "GLB_HEAD_JOINT", "Cannot find a retained Head joint for zero-weight fallback slots.", "$.skins[].joints");
            return std::nullopt;
        }

        std::vector<std::size_t> kept_ordinals;
        std::vector<std::size_t> ordinal_map(old_joints.size(), std::numeric_limits<std::size_t>::max());
        for (std::size_t ordinal = 0U; ordinal < old_joints.size(); ++ordinal) {
            if (removed_ordinals.contains(ordinal)) continue;
            ordinal_map[ordinal] = kept_ordinals.size();
            kept_ordinals.push_back(ordinal);
        }
        const std::size_t fallback_ordinal = ordinal_map[*head_ordinal];

        std::set<std::size_t> mesh_indices;
        for (const Json& node : nodes) {
            if (!node.is_object() || !node.contains("skin") || !node["skin"].is_number_unsigned()
                || node["skin"].get<std::size_t>() != skin_index
                || !node.contains("mesh") || !node["mesh"].is_number_unsigned()) continue;
            mesh_indices.insert(node["mesh"].get<std::size_t>());
        }
        if (!root.contains("meshes") || !root["meshes"].is_array()) {
            add_error(result, "GLB_MESHES", "A skinned GLB requires a mesh table.", "$.meshes");
            return std::nullopt;
        }
        for (const std::size_t mesh_index : mesh_indices) {
            if (mesh_index >= root["meshes"].size()
                || !root["meshes"][mesh_index].is_object()
                || !root["meshes"][mesh_index].contains("primitives")
                || !root["meshes"][mesh_index]["primitives"].is_array()) {
                add_error(result, "GLB_MESH", "Skinned node references an invalid mesh.", "$.meshes");
                return std::nullopt;
            }
            for (Json& primitive : root["meshes"][mesh_index]["primitives"]) {
                if (!primitive.is_object() || !primitive.contains("attributes")
                    || !primitive["attributes"].is_object()) continue;
                Json& attributes = primitive["attributes"];
                for (const std::string suffix : {std::string{"0"}, std::string{"1"}}) {
                    const std::string joints_key = "JOINTS_" + suffix;
                    const std::string weights_key = "WEIGHTS_" + suffix;
                    if (!attributes.contains(joints_key)) continue;
                    if (!attributes[joints_key].is_number_unsigned()
                        || !attributes.contains(weights_key)
                        || !attributes[weights_key].is_number_unsigned()) {
                        add_error(result, "GLB_SKIN_ATTRIBUTES", "JOINTS and WEIGHTS accessors must be paired.", "$.meshes[].primitives[].attributes");
                        return std::nullopt;
                    }
                    const std::size_t joints_accessor = attributes[joints_key].get<std::size_t>();
                    const std::size_t weights_accessor = attributes[weights_key].get<std::size_t>();
                    if (const auto existing = accessor_remaps.find(joints_accessor);
                        existing != accessor_remaps.end()) {
                        if (existing->second != ordinal_map) {
                            add_error(result, "GLB_SHARED_JOINT_ACCESSOR", "A JOINTS accessor is shared by incompatible skins.", "$.accessors");
                            return std::nullopt;
                        }
                        continue;
                    }
                    const auto joint_view = raw_accessor_view(root, joints_accessor, document.binary, result, "$.accessors[JOINTS]");
                    const auto weight_view = raw_accessor_view(root, weights_accessor, document.binary, result, "$.accessors[WEIGHTS]");
                    if (!joint_view.has_value() || !weight_view.has_value()) return std::nullopt;
                    if ((joint_view->component_type != unsigned_byte_component_type
                            && joint_view->component_type != unsigned_short_component_type)
                        || joint_view->components != 4U || weight_view->components != 4U
                        || joint_view->count != weight_view->count) {
                        add_error(result, "GLB_SKIN_ACCESSOR_LAYOUT", "Meshy helper removal requires matching VEC4 JOINTS/WEIGHTS accessors.", "$.accessors");
                        return std::nullopt;
                    }
                    for (std::size_t element = 0U; element < joint_view->count; ++element) {
                        for (std::size_t component = 0U; component < 4U; ++component) {
                            const auto source_ordinal = read_unsigned_component(
                                document.binary, *joint_view, element, component
                            );
                            const auto weight = read_weight_component(
                                document.binary, *weight_view, element, component
                            );
                            if (!source_ordinal.has_value() || !weight.has_value()
                                || *source_ordinal >= ordinal_map.size()) {
                                add_error(result, "GLB_SKIN_INFLUENCE", "Skin influence contains an invalid joint or weight.", "$.accessors[JOINTS]");
                                return std::nullopt;
                            }
                            std::size_t target_ordinal = ordinal_map[*source_ordinal];
                            if (removed_ordinals.contains(*source_ordinal)) {
                                if (*weight > 1.0e-8) {
                                    add_error(result, "GLB_WEIGHTED_HEAD_HELPER", "head_end/headfront carries a non-zero skin weight; automatic removal was refused.", "$.accessors[WEIGHTS]");
                                    return std::nullopt;
                                }
                                target_ordinal = fallback_ordinal;
                            }
                            if (!write_unsigned_component(
                                    document.binary,
                                    *joint_view,
                                    element,
                                    component,
                                    static_cast<std::uint32_t>(target_ordinal))) {
                                add_error(result, "GLB_JOINT_REMAP", "Cannot encode a remapped joint ordinal.", "$.accessors[JOINTS]");
                                return std::nullopt;
                            }
                        }
                    }
                    accessor_remaps.emplace(joints_accessor, ordinal_map);
                }
            }
        }

        if (!skin.contains("inverseBindMatrices") || !skin["inverseBindMatrices"].is_number_unsigned()) {
            add_error(result, "GLB_INVERSE_BIND", "Head helper removal requires explicit inverse bind matrices.", "$.skins[].inverseBindMatrices");
            return std::nullopt;
        }
        const std::size_t accessor_index = skin["inverseBindMatrices"].get<std::size_t>();
        if (compacted_inverse_bind_accessors.contains(accessor_index)) {
            add_error(result, "GLB_SHARED_INVERSE_BIND", "An inverse bind accessor is shared by multiple modified skins.", "$.skins[].inverseBindMatrices");
            return std::nullopt;
        }
        const auto matrices = accessor_view(root, accessor_index, document.binary, result, "$.skins[].inverseBindMatrices");
        if (!matrices.has_value() || matrices->components != 16U
            || matrices->count != old_joints.size()) {
            add_error(result, "GLB_INVERSE_BIND_COUNT", "Inverse bind matrix count must match the original joint count.", "$.skins[].inverseBindMatrices");
            return std::nullopt;
        }
        constexpr std::size_t matrix_size = 16U * sizeof(float);
        for (std::size_t target = 0U; target < kept_ordinals.size(); ++target) {
            const std::size_t source = kept_ordinals[target];
            std::memmove(
                document.binary.data() + matrices->offset + target * matrices->stride,
                document.binary.data() + matrices->offset + source * matrices->stride,
                matrix_size
            );
        }
        root["accessors"][accessor_index]["count"] = kept_ordinals.size();
        compacted_inverse_bind_accessors.insert(accessor_index);

        Json kept_joints = Json::array();
        for (const std::size_t ordinal : kept_ordinals) kept_joints.push_back(old_joints[ordinal]);
        skin["joints"] = std::move(kept_joints);
        counts.removed_joint_count += removed_ordinals.size();
    }

    if (root.contains("animations")) {
        if (!root["animations"].is_array()) {
            add_error(result, "GLB_ANIMATIONS", "Animations must be an array.", "$.animations");
            return std::nullopt;
        }
        Json kept_animations = Json::array();
        for (Json& animation : root["animations"]) {
            if (!animation.is_object() || !animation.contains("channels")
                || !animation["channels"].is_array() || !animation.contains("samplers")
                || !animation["samplers"].is_array()) {
                add_error(result, "GLB_ANIMATION_LAYOUT", "Animation channels and samplers are required.", "$.animations");
                return std::nullopt;
            }
            Json kept_channels = Json::array();
            std::set<std::size_t> used_samplers;
            for (const Json& channel : animation["channels"]) {
                if (!channel.is_object() || !channel.contains("sampler")
                    || !channel["sampler"].is_number_unsigned()
                    || !channel.contains("target") || !channel["target"].is_object()
                    || !channel["target"].contains("node")
                    || !channel["target"]["node"].is_number_unsigned()) {
                    add_error(result, "GLB_ANIMATION_CHANNEL", "Animation channel is malformed.", "$.animations[].channels");
                    return std::nullopt;
                }
                const std::size_t sampler = channel["sampler"].get<std::size_t>();
                if (sampler >= animation["samplers"].size()) {
                    add_error(result, "GLB_ANIMATION_SAMPLER", "Animation channel references an invalid sampler.", "$.animations[].channels");
                    return std::nullopt;
                }
                const std::size_t target_node = channel["target"]["node"].get<std::size_t>();
                if (removed_nodes.contains(target_node)) {
                    ++counts.removed_animation_channel_count;
                    continue;
                }
                used_samplers.insert(sampler);
                kept_channels.push_back(channel);
            }
            if (kept_channels.empty()) continue;
            std::map<std::size_t, std::size_t> sampler_map;
            Json kept_samplers = Json::array();
            for (const std::size_t source : used_samplers) {
                sampler_map.emplace(source, kept_samplers.size());
                kept_samplers.push_back(animation["samplers"][source]);
            }
            for (Json& channel : kept_channels) {
                channel["sampler"] = sampler_map.at(channel["sampler"].get<std::size_t>());
            }
            animation["channels"] = std::move(kept_channels);
            animation["samplers"] = std::move(kept_samplers);
            kept_animations.push_back(animation);
        }
        if (kept_animations.empty()) root.erase("animations");
        else root["animations"] = std::move(kept_animations);
    }

    for (Json& node : nodes) {
        if (!node.is_object() || !node.contains("children")) continue;
        if (!node["children"].is_array()) {
            add_error(result, "GLB_NODE_CHILDREN", "Node children must be an array.", "$.nodes[].children");
            return std::nullopt;
        }
        Json children = Json::array();
        for (const Json& child : node["children"]) {
            if (!child.is_number_unsigned()) {
                add_error(result, "GLB_NODE_CHILD", "Node child index must be unsigned.", "$.nodes[].children");
                return std::nullopt;
            }
            if (!removed_nodes.contains(child.get<std::size_t>())) children.push_back(child);
        }
        if (children.empty()) node.erase("children");
        else node["children"] = std::move(children);
    }

    std::vector<std::size_t> node_map(nodes.size(), std::numeric_limits<std::size_t>::max());
    Json kept_nodes = Json::array();
    for (std::size_t source = 0U; source < nodes.size(); ++source) {
        if (removed_nodes.contains(source)) continue;
        node_map[source] = kept_nodes.size();
        kept_nodes.push_back(nodes[source]);
    }
    const auto remap_node = [&](Json& value, const std::string& path) -> bool {
        if (!value.is_number_unsigned()) {
            add_error(result, "GLB_NODE_INDEX", "Node reference must be unsigned.", path);
            return false;
        }
        const std::size_t source = value.get<std::size_t>();
        if (source >= node_map.size() || node_map[source] == std::numeric_limits<std::size_t>::max()) {
            add_error(result, "GLB_NODE_INDEX", "Node reference targets a removed or invalid node.", path);
            return false;
        }
        value = node_map[source];
        return true;
    };
    for (Json& node : kept_nodes) {
        if (!node.contains("children")) continue;
        for (Json& child : node["children"]) {
            if (!remap_node(child, "$.nodes[].children")) return std::nullopt;
        }
    }
    if (root.contains("scenes")) {
        if (!root["scenes"].is_array()) {
            add_error(result, "GLB_SCENES", "Scenes must be an array.", "$.scenes");
            return std::nullopt;
        }
        for (Json& scene : root["scenes"]) {
            if (!scene.is_object() || !scene.contains("nodes")) continue;
            if (!scene["nodes"].is_array()) {
                add_error(result, "GLB_SCENE_NODES", "Scene nodes must be an array.", "$.scenes[].nodes");
                return std::nullopt;
            }
            Json scene_nodes = Json::array();
            for (Json node : scene["nodes"]) {
                if (node.is_number_unsigned() && removed_nodes.contains(node.get<std::size_t>())) continue;
                if (!remap_node(node, "$.scenes[].nodes")) return std::nullopt;
                scene_nodes.push_back(std::move(node));
            }
            if (scene_nodes.empty()) scene.erase("nodes");
            else scene["nodes"] = std::move(scene_nodes);
        }
    }
    for (Json& skin : root["skins"]) {
        for (Json& joint : skin["joints"]) {
            if (!remap_node(joint, "$.skins[].joints")) return std::nullopt;
        }
        if (skin.contains("skeleton") && !remap_node(skin["skeleton"], "$.skins[].skeleton")) {
            return std::nullopt;
        }
    }
    if (root.contains("animations")) {
        for (Json& animation : root["animations"]) {
            for (Json& channel : animation["channels"]) {
                if (!remap_node(channel["target"]["node"], "$.animations[].channels[].target.node")) {
                    return std::nullopt;
                }
            }
        }
    }
    root["nodes"] = std::move(kept_nodes);
    counts.removed_node_count = removed_nodes.size();
    return counts;
}

std::optional<AnimationRemovalCounts> remove_all_animations(
    Json& root,
    GlbSpatialNormalizationResult& result
) {
    AnimationRemovalCounts counts;
    if (!root.contains("animations")) return counts;
    if (!root["animations"].is_array()) {
        add_error(result, "GLB_ANIMATIONS", "Animations must be an array.", "$.animations");
        return std::nullopt;
    }
    counts.removed_clip_count = root["animations"].size();
    for (const Json& animation : root["animations"]) {
        if (!animation.is_object() || !animation.contains("channels")
            || !animation["channels"].is_array() || !animation.contains("samplers")
            || !animation["samplers"].is_array()) {
            add_error(result, "GLB_ANIMATION_LAYOUT", "Animation channels and samplers are required.", "$.animations");
            return std::nullopt;
        }
        counts.removed_channel_count += animation["channels"].size();
        counts.removed_sampler_count += animation["samplers"].size();
    }
    root.erase("animations");
    return counts;
}

std::vector<int> node_parents(const Json& nodes, GlbSpatialNormalizationResult& result) {
    std::vector<int> parents(nodes.size(), -1);
    for (std::size_t parent = 0U; parent < nodes.size(); ++parent) {
        if (!nodes[parent].is_object() || !nodes[parent].contains("children")) {
            continue;
        }
        if (!nodes[parent]["children"].is_array()) {
            add_error(result, "GLB_NODE_CHILDREN", "Node children must be an array.");
            return {};
        }
        for (const Json& child_value : nodes[parent]["children"]) {
            if (!child_value.is_number_unsigned()) {
                add_error(result, "GLB_NODE_CHILD", "Node child index must be unsigned.");
                return {};
            }
            const std::size_t child = child_value.get<std::size_t>();
            if (child >= nodes.size() || parents[child] != -1) {
                add_error(result, "GLB_NODE_HIERARCHY", "Node hierarchy contains an invalid child or multiple parents.");
                return {};
            }
            parents[child] = static_cast<int>(parent);
        }
    }
    return parents;
}

bool descendant_of(const std::size_t node, const std::size_t ancestor,
                   const std::vector<int>& parents) {
    std::size_t current = node;
    while (true) {
        if (current == ancestor) {
            return true;
        }
        if (current >= parents.size() || parents[current] < 0) {
            return false;
        }
        current = static_cast<std::size_t>(parents[current]);
    }
}

std::optional<double> uniform_scale(const Json& node) {
    if (!node.is_object() || !node.contains("scale") || !node["scale"].is_array()
        || node["scale"].size() != 3U) {
        return std::nullopt;
    }
    std::array<double, 3> scale{};
    for (std::size_t index = 0U; index < scale.size(); ++index) {
        if (!node["scale"][index].is_number()) {
            return std::nullopt;
        }
        scale[index] = node["scale"][index].get<double>();
    }
    if (!std::isfinite(scale[0]) || scale[0] <= 0.0
        || std::abs(scale[0] - scale[1]) > 1.0e-6
        || std::abs(scale[0] - scale[2]) > 1.0e-6
        || std::abs(scale[0] - 1.0) <= 1.0e-6) {
        return std::nullopt;
    }
    return scale[0];
}

bool identity_root_pose(const Json& node) {
    const auto zero_translation = [&]() {
        if (!node.contains("translation")) return true;
        if (!node["translation"].is_array() || node["translation"].size() != 3U) return false;
        for (const Json& value : node["translation"]) {
            if (!value.is_number() || std::abs(value.get<double>()) > 1.0e-9) return false;
        }
        return true;
    }();
    const auto identity_rotation = [&]() {
        if (!node.contains("rotation")) return true;
        if (!node["rotation"].is_array() || node["rotation"].size() != 4U) return false;
        constexpr std::array<double, 4> expected{0.0, 0.0, 0.0, 1.0};
        for (std::size_t index = 0U; index < expected.size(); ++index) {
            if (!node["rotation"][index].is_number()
                || std::abs(node["rotation"][index].get<double>() - expected[index]) > 1.0e-9) return false;
        }
        return true;
    }();
    return zero_translation && identity_rotation && !node.contains("matrix");
}

std::optional<std::size_t> find_mixed_unit_root(
    const Json& root,
    const std::vector<int>& parents,
    GlbSpatialNormalizationResult& result
) {
    const Json& nodes = root["nodes"];
    std::vector<std::size_t> skinned_nodes;
    for (std::size_t index = 0U; index < nodes.size(); ++index) {
        if (nodes[index].is_object() && nodes[index].contains("mesh")
            && nodes[index].contains("skin")) {
            skinned_nodes.push_back(index);
        }
    }
    if (skinned_nodes.empty() || !root.contains("skins") || !root["skins"].is_array()
        || root["skins"].empty()) {
        add_error(result, "GLB_SKIN_REQUIRED", "Mixed-unit rig normalization requires a skinned GLB.");
        return std::nullopt;
    }

    std::vector<std::size_t> candidates;
    for (std::size_t index = 0U; index < nodes.size(); ++index) {
        if (parents[index] != -1 || !uniform_scale(nodes[index]).has_value()) {
            continue;
        }
        const bool owns_meshes = std::ranges::all_of(skinned_nodes, [&](const std::size_t node) {
            return descendant_of(node, index, parents);
        });
        bool owns_joints = true;
        for (const Json& skin : root["skins"]) {
            if (!skin.is_object() || !skin.contains("joints") || !skin["joints"].is_array()) {
                owns_joints = false;
                break;
            }
            for (const Json& joint : skin["joints"]) {
                if (!joint.is_number_unsigned()
                    || joint.get<std::size_t>() >= nodes.size()
                    || !descendant_of(joint.get<std::size_t>(), index, parents)) {
                    owns_joints = false;
                    break;
                }
            }
        }
        if (owns_meshes && owns_joints) {
            candidates.push_back(index);
        }
    }
    if (candidates.size() != 1U) {
        add_error(
            result,
            "GLB_MIXED_UNIT_ROOT",
            candidates.empty()
                ? "No unique uniformly-scaled root owns every skinned mesh and joint."
                : "More than one uniformly-scaled root could own the rig."
        );
        return std::nullopt;
    }
    if (!identity_root_pose(nodes[candidates.front()])) {
        add_error(result, "GLB_ROOT_TRANSFORM", "The mixed-unit root must have identity translation/rotation and no matrix.");
        return std::nullopt;
    }
    return candidates.front();
}

std::optional<double> position_height(
    const Json& root,
    const std::vector<std::byte>& binary,
    GlbSpatialNormalizationResult& result
) {
    if (!root.contains("meshes") || !root["meshes"].is_array()) {
        add_error(result, "GLB_MESHES", "GLB contains no mesh table.");
        return std::nullopt;
    }
    std::set<std::size_t> accessors;
    for (const Json& mesh : root["meshes"]) {
        if (!mesh.is_object() || !mesh.contains("primitives") || !mesh["primitives"].is_array()) continue;
        for (const Json& primitive : mesh["primitives"]) {
            if (primitive.is_object() && primitive.contains("attributes")
                && primitive["attributes"].is_object()
                && primitive["attributes"].contains("POSITION")
                && primitive["attributes"]["POSITION"].is_number_unsigned()) {
                accessors.insert(primitive["attributes"]["POSITION"].get<std::size_t>());
            }
        }
    }
    double minimum = std::numeric_limits<double>::infinity();
    double maximum = -std::numeric_limits<double>::infinity();
    for (const std::size_t accessor_index : accessors) {
        const auto view = accessor_view(root, accessor_index, binary, result, "$.accessors[POSITION]");
        if (!view.has_value() || view->components != 3U) return std::nullopt;
        for (std::size_t element = 0U; element < view->count; ++element) {
            const auto y = read_float(binary, view->offset + element * view->stride + sizeof(float));
            if (!y.has_value() || !std::isfinite(*y)) {
                add_error(result, "GLB_POSITION", "POSITION accessor contains an invalid Y component.");
                return std::nullopt;
            }
            minimum = std::min(minimum, static_cast<double>(*y));
            maximum = std::max(maximum, static_cast<double>(*y));
        }
    }
    if (accessors.empty() || !std::isfinite(minimum) || maximum <= minimum) {
        add_error(result, "GLB_POSITION_HEIGHT", "Cannot determine a positive POSITION height.");
        return std::nullopt;
    }
    return maximum - minimum;
}

std::vector<std::byte> encode_glb(const GlbDocument& document) {
    std::string json = document.json.dump();
    while (json.size() % 4U != 0U) json.push_back(' ');
    std::vector<std::byte> binary = document.binary;
    while (binary.size() % 4U != 0U) binary.push_back(std::byte{0});
    const std::uint64_t total = 12ULL + 8ULL + json.size() + 8ULL + binary.size();
    if (total > std::numeric_limits<std::uint32_t>::max()) return {};
    std::vector<std::byte> output;
    output.reserve(static_cast<std::size_t>(total));
    append_u32(output, glb_magic);
    append_u32(output, glb_version);
    append_u32(output, static_cast<std::uint32_t>(total));
    append_u32(output, static_cast<std::uint32_t>(json.size()));
    append_u32(output, json_chunk_type);
    output.insert(
        output.end(),
        reinterpret_cast<const std::byte*>(json.data()),
        reinterpret_cast<const std::byte*>(json.data() + json.size())
    );
    append_u32(output, static_cast<std::uint32_t>(binary.size()));
    append_u32(output, bin_chunk_type);
    output.insert(output.end(), binary.begin(), binary.end());
    return output;
}

bool write_atomic(
    const std::filesystem::path& output_path,
    const std::vector<std::byte>& bytes,
    const bool overwrite,
    GlbSpatialNormalizationResult& result
) {
    std::error_code error;
    if (std::filesystem::exists(output_path, error) && !overwrite) {
        add_error(result, "OUTPUT_EXISTS", "Output already exists and overwrite is disabled.", "$.output_path");
        return false;
    }
    if (!output_path.parent_path().empty()) {
        std::filesystem::create_directories(output_path.parent_path(), error);
        if (error) {
            add_error(result, "OUTPUT_DIRECTORY", error.message(), "$.output_path");
            return false;
        }
    }
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path temporary = output_path.string()
        + ".tmp-" + std::to_string(stamp);
    {
        std::ofstream output{temporary, std::ios::binary | std::ios::trunc};
        if (!output) {
            add_error(result, "OUTPUT_OPEN", "Cannot open temporary output file.", "$.output_path");
            return false;
        }
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!output) {
            add_error(result, "OUTPUT_WRITE", "Cannot write normalized GLB.", "$.output_path");
            std::filesystem::remove(temporary, error);
            return false;
        }
    }
    if (overwrite && std::filesystem::exists(output_path, error)) {
        std::filesystem::remove(output_path, error);
        if (error) {
            add_error(result, "OUTPUT_REPLACE", error.message(), "$.output_path");
            std::filesystem::remove(temporary, error);
            return false;
        }
    }
    std::filesystem::rename(temporary, output_path, error);
    if (error) {
        add_error(result, "OUTPUT_RENAME", error.message(), "$.output_path");
        std::filesystem::remove(temporary, error);
        return false;
    }
    return true;
}

}  // namespace

bool GlbSpatialNormalizationResult::success() const noexcept {
    return report.has_value() && std::ranges::none_of(
        diagnostics,
        [](const Diagnostic& diagnostic) {
            return diagnostic.severity == DiagnosticSeverity::error;
        }
    );
}

GlbSpatialNormalizationResult normalize_mixed_unit_rig_glb(
    const std::filesystem::path& input_path,
    const std::filesystem::path& output_path,
    const GlbSpatialNormalizationOptions& options
) {
    GlbSpatialNormalizationResult result;
    if (!options.correct_scale_factor && !options.remove_emissive_channel
        && !options.remove_head_helper_bones && !options.remove_animations) {
        add_error(
            result,
            "NO_CORRECTION_SELECTED",
            "At least one scale, emissive, head-helper or animation correction must be enabled."
        );
        return result;
    }
    if (options.correct_scale_factor
        && (!std::isfinite(options.height_tolerance_m)
            || options.height_tolerance_m <= 0.0)) {
        add_error(result, "HEIGHT_TOLERANCE", "Height tolerance must be finite and positive.");
        return result;
    }
    if (options.correct_scale_factor
        && options.expected_position_height_m.has_value()
        && (!std::isfinite(*options.expected_position_height_m)
            || *options.expected_position_height_m <= 0.0)) {
        add_error(result, "EXPECTED_HEIGHT", "Expected height must be finite and positive.");
        return result;
    }
    std::error_code error;
    const std::filesystem::path input = std::filesystem::weakly_canonical(
        std::filesystem::absolute(input_path, error), error
    );
    if (error || !std::filesystem::is_regular_file(input, error)
        || input.extension() != ".glb") {
        add_error(result, "INPUT_PATH", "Input must be an existing local .glb file.", "$.input_path");
        return result;
    }
    const std::filesystem::path output = std::filesystem::absolute(output_path, error).lexically_normal();
    if (error || output.extension() != ".glb") {
        add_error(result, "OUTPUT_PATH", "Output must use the .glb extension.", "$.output_path");
        return result;
    }
    if (std::filesystem::equivalent(input, output, error) && !error) {
        add_error(result, "OUTPUT_EQUALS_INPUT", "Normalization never overwrites the source GLB in place.", "$.output_path");
        return result;
    }

    auto source = read_file(input);
    if (!source.has_value()) {
        add_error(result, "INPUT_READ", "Cannot read the input GLB.", "$.input_path");
        return result;
    }
    auto document = decode_glb(*source, result);
    if (!document.has_value()) return result;
    Json& root = document->json;
    if (!root.contains("asset") || !root["asset"].is_object()
        || root["asset"].value("version", std::string{}) != "2.0"
        || !root.contains("nodes") || !root["nodes"].is_array()) {
        add_error(result, "GLTF_DOCUMENT", "A glTF 2.0 node hierarchy is required.");
        return result;
    }
    const auto height = position_height(root, document->binary, result);
    if (!height.has_value()) return result;
    if (options.correct_scale_factor
        && options.expected_position_height_m.has_value()
        && std::abs(*height - *options.expected_position_height_m)
            > options.height_tolerance_m) {
        add_error(
            result,
            "POSITION_HEIGHT_MISMATCH",
            "POSITION height is outside the expected humanoid safety window; no correction was written.",
            "$.expected_position_height_m"
        );
        return result;
    }

    HeadHelperRemovalCounts head_helper_counts;
    if (options.remove_head_helper_bones) {
        const auto removed = remove_meshy_head_helpers(*document, result);
        if (!removed.has_value()) return result;
        head_helper_counts = *removed;
    }
    AnimationRemovalCounts animation_counts;
    if (options.remove_animations) {
        const auto removed = remove_all_animations(root, result);
        if (!removed.has_value()) return result;
        animation_counts = *removed;
    }
    EmissiveCorrectionCounts emissive_counts;
    if (options.remove_emissive_channel) {
        const auto corrected = disable_material_emission(root, result);
        if (!corrected.has_value()) return result;
        emissive_counts = *corrected;
    }

    if (!options.correct_scale_factor) {
        root["asset"]["generator"] = root["asset"].value("generator", std::string{})
            + " | Unified3D Meshy material/rig/animation corrector 0.6.0-dev";
        if (!root["asset"].contains("extras") || !root["asset"]["extras"].is_object()) {
            root["asset"]["extras"] = Json::object();
        }
        root["asset"]["extras"]["unified3dSpatialNormalization"] = Json{
            {"schema", "unified3d.spatial-normalization/1.0-draft"},
            {"operation", "apply_selected_meshy_corrections"},
            {"correctScaleFactor", false},
            {"removeEmissiveChannel", options.remove_emissive_channel},
            {"removeHeadHelperBones", options.remove_head_helper_bones},
            {"removeAnimations", options.remove_animations},
            {"positionHeightMeters", *height},
            {"removedEmissiveTextures", emissive_counts.removed_texture_count},
            {"zeroedEmissiveFactors", emissive_counts.zeroed_factor_count},
            {"removedHeadHelperNodes", head_helper_counts.removed_node_count},
            {"removedHeadHelperJoints", head_helper_counts.removed_joint_count},
            {"removedHeadHelperAnimationChannels", head_helper_counts.removed_animation_channel_count},
            {"removedAnimationClips", animation_counts.removed_clip_count},
            {"removedAnimationChannels", animation_counts.removed_channel_count},
            {"removedAnimationSamplers", animation_counts.removed_sampler_count},
        };

        std::vector<std::byte> encoded = encode_glb(*document);
        if (encoded.empty()) {
            add_error(result, "GLB_OUTPUT_SIZE", "Corrected GLB exceeds the 32-bit GLB container limit.");
            return result;
        }
        if (!write_atomic(output, encoded, options.overwrite, result)) return result;

        result.report = GlbSpatialNormalizationReport{
            .source_path = input,
            .output_path = output,
            .source_size_bytes = static_cast<std::uint64_t>(source->size()),
            .output_size_bytes = static_cast<std::uint64_t>(encoded.size()),
            .root_node_index = 0U,
            .root_node_name = {},
            .absorbed_uniform_scale = 1.0,
            .position_height_m = *height,
            .modified_node_translation_count = 0U,
            .modified_animation_accessor_count = 0U,
            .modified_inverse_bind_matrix_count = 0U,
            .removed_emissive_texture_count = emissive_counts.removed_texture_count,
            .zeroed_emissive_factor_count = emissive_counts.zeroed_factor_count,
            .removed_head_helper_node_count = head_helper_counts.removed_node_count,
            .removed_head_helper_joint_count = head_helper_counts.removed_joint_count,
            .removed_head_helper_animation_channel_count = head_helper_counts.removed_animation_channel_count,
            .removed_animation_clip_count = animation_counts.removed_clip_count,
            .removed_animation_channel_count = animation_counts.removed_channel_count,
            .removed_animation_sampler_count = animation_counts.removed_sampler_count,
            .scale_correction_applied = false,
            .emissive_correction_applied = options.remove_emissive_channel,
            .head_helper_bone_removal_applied = options.remove_head_helper_bones,
            .animation_removal_applied = options.remove_animations,
        };
        return result;
    }

    const std::vector<int> parents = node_parents(root["nodes"], result);
    if (!result.diagnostics.empty()) return result;
    const auto root_node = find_mixed_unit_root(root, parents, result);
    if (!root_node.has_value()) return result;
    const double factor = *uniform_scale(root["nodes"][*root_node]);
    constexpr std::array<double, 6> unit_scale_candidates{
        0.001, 0.01, 0.1, 10.0, 100.0, 1000.0,
    };
    const bool recognized_unit_scale = std::ranges::any_of(
        unit_scale_candidates,
        [factor](const double candidate) {
            return std::abs(factor - candidate) <= candidate * 1.0e-5;
        }
    );
    if (!recognized_unit_scale) {
        add_error(
            result,
            "GLB_ROOT_SCALE_NOT_UNIT_CONVERSION",
            "The root scale is not a recognized power-of-ten unit conversion; automatic absorption was refused.",
            "$.nodes[root].scale"
        );
        return result;
    }

    std::set<std::size_t> animated_root_targets;
    std::set<std::size_t> translation_accessors;
    std::set<std::size_t> other_output_accessors;
    if (root.contains("animations")) {
        if (!root["animations"].is_array()) {
            add_error(result, "GLB_ANIMATIONS", "Animations must be an array.");
            return result;
        }
        for (std::size_t animation_index = 0U; animation_index < root["animations"].size(); ++animation_index) {
            const Json& animation = root["animations"][animation_index];
            if (!animation.is_object() || !animation.contains("channels")
                || !animation["channels"].is_array() || !animation.contains("samplers")
                || !animation["samplers"].is_array()) {
                add_error(result, "GLB_ANIMATION_LAYOUT", "Animation channels and samplers are required.");
                return result;
            }
            for (const Json& channel : animation["channels"]) {
                if (!channel.is_object() || !channel.contains("sampler")
                    || !channel["sampler"].is_number_unsigned()
                    || !channel.contains("target") || !channel["target"].is_object()) {
                    add_error(result, "GLB_ANIMATION_CHANNEL", "Animation channel is malformed.");
                    return result;
                }
                const Json& target = channel["target"];
                if (!target.contains("node") || !target["node"].is_number_unsigned()
                    || !target.contains("path") || !target["path"].is_string()) {
                    add_error(result, "GLB_ANIMATION_TARGET", "Node animation target is required.");
                    return result;
                }
                const std::size_t node = target["node"].get<std::size_t>();
                if (node >= root["nodes"].size()) {
                    add_error(result, "GLB_ANIMATION_NODE", "Animation target node is invalid.");
                    return result;
                }
                if (node == *root_node) animated_root_targets.insert(node);
                const std::size_t sampler_index = channel["sampler"].get<std::size_t>();
                if (sampler_index >= animation["samplers"].size()
                    || !animation["samplers"][sampler_index].contains("output")
                    || !animation["samplers"][sampler_index]["output"].is_number_unsigned()) {
                    add_error(result, "GLB_ANIMATION_SAMPLER", "Animation sampler output is invalid.");
                    return result;
                }
                const std::size_t accessor = animation["samplers"][sampler_index]["output"].get<std::size_t>();
                if (target["path"] == "translation" && descendant_of(node, *root_node, parents)
                    && node != *root_node) {
                    translation_accessors.insert(accessor);
                } else {
                    other_output_accessors.insert(accessor);
                }
            }
        }
    }
    if (!animated_root_targets.empty()) {
        add_error(result, "GLB_ANIMATED_ROOT", "The mixed-unit root is animated; automatic absorption is ambiguous.");
        return result;
    }
    std::vector<std::size_t> shared_accessors;
    std::ranges::set_intersection(
        translation_accessors, other_output_accessors,
        std::back_inserter(shared_accessors)
    );
    if (!shared_accessors.empty()) {
        add_error(result, "GLB_SHARED_ANIMATION_ACCESSOR", "A translation output accessor is shared by a non-translation channel.");
        return result;
    }

    std::uint64_t node_translation_count = 0U;
    for (std::size_t node_index = 0U; node_index < root["nodes"].size(); ++node_index) {
        if (node_index == *root_node || !descendant_of(node_index, *root_node, parents)) continue;
        Json& node = root["nodes"][node_index];
        if (node.contains("matrix")) {
            add_error(result, "GLB_DESCENDANT_MATRIX", "A descendant uses a matrix transform that cannot be safely rescaled.");
            return result;
        }
        if (!node.contains("translation")) continue;
        if (!node["translation"].is_array() || node["translation"].size() != 3U) {
            add_error(result, "GLB_NODE_TRANSLATION", "Node translation must be a VEC3.");
            return result;
        }
        for (Json& value : node["translation"]) {
            if (!value.is_number() || !std::isfinite(value.get<double>())) {
                add_error(result, "GLB_NODE_TRANSLATION", "Node translation contains a non-finite component.");
                return result;
            }
            value = value.get<double>() * factor;
        }
        ++node_translation_count;
    }

    for (const std::size_t accessor_index : translation_accessors) {
        const auto view = accessor_view(root, accessor_index, document->binary, result, "$.animations[].translation");
        if (!view.has_value() || view->components != 3U
            || !scale_accessor_components(document->binary, *view, factor, {0U, 1U, 2U})) {
            add_error(result, "GLB_ANIMATION_TRANSLATION", "Cannot rescale a translation animation accessor.");
            return result;
        }
    }

    std::set<std::size_t> inverse_bind_accessors;
    for (const Json& skin : root["skins"]) {
        if (!skin.is_object() || !skin.contains("inverseBindMatrices")
            || !skin["inverseBindMatrices"].is_number_unsigned()) {
            add_error(result, "GLB_INVERSE_BIND", "Each skin requires explicit inverse bind matrices.");
            return result;
        }
        inverse_bind_accessors.insert(skin["inverseBindMatrices"].get<std::size_t>());
    }
    // Meshy/Blender mixed-unit exports store the complete affine part of each
    // inverse bind matrix in the legacy unit domain. The known-good Rig-Safe
    // correction scales all 15 affine components and preserves only the final
    // homogeneous component at 1.0. Restricting the correction to translation
    // components 12..14 leaves the skin in the old scale domain and regresses
    // the character resizing even though the scene root becomes identity.
    const std::set<std::size_t> inverse_bind_affine_components{
        0U, 1U, 2U, 3U,
        4U, 5U, 6U, 7U,
        8U, 9U, 10U, 11U,
        12U, 13U, 14U,
    };
    std::uint64_t matrix_count = 0U;
    for (const std::size_t accessor_index : inverse_bind_accessors) {
        const auto view = accessor_view(root, accessor_index, document->binary, result, "$.skins[].inverseBindMatrices");
        if (!view.has_value() || view->components != 16U
            || !scale_accessor_components(
                document->binary,
                *view,
                factor,
                inverse_bind_affine_components)) {
            add_error(result, "GLB_INVERSE_BIND", "Cannot rescale inverse bind matrices.");
            return result;
        }
        matrix_count += view->count;
    }

    Json& mixed_root = root["nodes"][*root_node];
    mixed_root["scale"] = Json::array({1.0, 1.0, 1.0});
    root["asset"]["generator"] = root["asset"].value("generator", std::string{})
        + " | Unified3D mixed-unit Meshy material/rig/animation corrector 0.6.0-dev";
    if (!root["asset"].contains("extras") || !root["asset"]["extras"].is_object()) {
        root["asset"]["extras"] = Json::object();
    }
    root["asset"]["extras"]["unified3dSpatialNormalization"] = Json{
        {"schema", "unified3d.spatial-normalization/1.0-draft"},
        {"operation", "absorb_mixed_unit_root_scale_and_apply_selected_meshy_corrections"},
        {"correctScaleFactor", true},
        {"removeEmissiveChannel", options.remove_emissive_channel},
        {"removeHeadHelperBones", options.remove_head_helper_bones},
        {"removeAnimations", options.remove_animations},
        {"rootNode", *root_node},
        {"absorbedScale", factor},
        {"positionHeightMeters", *height},
        {"removedEmissiveTextures", emissive_counts.removed_texture_count},
        {"zeroedEmissiveFactors", emissive_counts.zeroed_factor_count},
        {"removedHeadHelperNodes", head_helper_counts.removed_node_count},
        {"removedHeadHelperJoints", head_helper_counts.removed_joint_count},
        {"removedHeadHelperAnimationChannels", head_helper_counts.removed_animation_channel_count},
        {"removedAnimationClips", animation_counts.removed_clip_count},
        {"removedAnimationChannels", animation_counts.removed_channel_count},
        {"removedAnimationSamplers", animation_counts.removed_sampler_count},
    };

    std::vector<std::byte> encoded = encode_glb(*document);
    if (encoded.empty()) {
        add_error(result, "GLB_OUTPUT_SIZE", "Normalized GLB exceeds the 32-bit GLB container limit.");
        return result;
    }
    if (!write_atomic(output, encoded, options.overwrite, result)) return result;

    result.report = GlbSpatialNormalizationReport{
        .source_path = input,
        .output_path = output,
        .source_size_bytes = static_cast<std::uint64_t>(source->size()),
        .output_size_bytes = static_cast<std::uint64_t>(encoded.size()),
        .root_node_index = *root_node,
        .root_node_name = mixed_root.value("name", std::string{}),
        .absorbed_uniform_scale = factor,
        .position_height_m = *height,
        .modified_node_translation_count = node_translation_count,
        .modified_animation_accessor_count = static_cast<std::uint64_t>(translation_accessors.size()),
        .modified_inverse_bind_matrix_count = matrix_count,
        .removed_emissive_texture_count = emissive_counts.removed_texture_count,
        .zeroed_emissive_factor_count = emissive_counts.zeroed_factor_count,
        .removed_head_helper_node_count = head_helper_counts.removed_node_count,
        .removed_head_helper_joint_count = head_helper_counts.removed_joint_count,
        .removed_head_helper_animation_channel_count = head_helper_counts.removed_animation_channel_count,
        .removed_animation_clip_count = animation_counts.removed_clip_count,
        .removed_animation_channel_count = animation_counts.removed_channel_count,
        .removed_animation_sampler_count = animation_counts.removed_sampler_count,
        .scale_correction_applied = true,
        .emissive_correction_applied = options.remove_emissive_channel,
        .head_helper_bone_removal_applied = options.remove_head_helper_bones,
        .animation_removal_applied = options.remove_animations,
    };
    return result;
}

}  // namespace unified3d::runtime
