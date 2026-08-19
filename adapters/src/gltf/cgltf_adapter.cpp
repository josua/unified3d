#include "../adapter_internal.hpp"

#include <cgltf.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace unified3d::adapters::detail {
namespace {

using geometry::BufferView;
using geometry::IndexBuffer;
using geometry::PrimitiveBuffers;
using geometry::ScalarType;
using geometry::SkinInfluenceSet;
using geometry::VertexAttributeBuffer;
using geometry::VertexSemantic;

std::string utf8_path(const std::filesystem::path& path) {
#if defined(_WIN32)
    const std::u8string value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
#else
    return path.string();
#endif
}

const char* result_name(const cgltf_result result) {
    switch (result) {
        case cgltf_result_success: return "success";
        case cgltf_result_data_too_short: return "data_too_short";
        case cgltf_result_unknown_format: return "unknown_format";
        case cgltf_result_invalid_json: return "invalid_json";
        case cgltf_result_invalid_gltf: return "invalid_gltf";
        case cgltf_result_invalid_options: return "invalid_options";
        case cgltf_result_file_not_found: return "file_not_found";
        case cgltf_result_io_error: return "io_error";
        case cgltf_result_out_of_memory: return "out_of_memory";
        case cgltf_result_legacy_gltf: return "legacy_gltf";
        case cgltf_result_max_enum: return "unknown";
    }
    return "unknown";
}

VertexAttributeBuffer make_attribute(
    const VertexSemantic semantic,
    const ScalarType scalar_type,
    const std::uint32_t component_count,
    const std::uint64_t element_count,
    geometry::ImmutableBytes storage
) {
    return VertexAttributeBuffer{
        .semantic = semantic,
        .view = BufferView{
            .storage = std::move(storage),
            .byte_offset = 0U,
            .byte_stride = 0U,
            .element_count = element_count,
            .component_count = component_count,
            .scalar_type = scalar_type,
        },
    };
}

bool compressed_accessor(const cgltf_accessor* accessor) {
    return accessor != nullptr && accessor->buffer_view != nullptr
        && accessor->buffer_view->has_meshopt_compression;
}

std::vector<std::uint32_t> triangle_indices(
    const cgltf_primitive& primitive,
    const std::uint64_t vertex_count,
    std::vector<Diagnostic>& diagnostics,
    const std::string& path
) {
    std::vector<std::uint32_t> source;
    const std::uint64_t source_count = primitive.indices != nullptr
        ? static_cast<std::uint64_t>(primitive.indices->count)
        : vertex_count;
    if (source_count > std::numeric_limits<std::uint32_t>::max()) {
        diagnostics.push_back(error("GLTF_INDEX_RANGE", "Index count exceeds uint32 capacity.", path));
        return {};
    }
    source.resize(static_cast<std::size_t>(source_count));
    if (primitive.indices != nullptr) {
        if (compressed_accessor(primitive.indices)) {
            diagnostics.push_back(error("GLTF_MESHOPT_UNSUPPORTED", "Meshopt-compressed index buffers require a decoder.", path));
            return {};
        }
        for (std::size_t index = 0U; index < source.size(); ++index) {
            const cgltf_size value = cgltf_accessor_read_index(primitive.indices, index);
            if (value > std::numeric_limits<std::uint32_t>::max()) {
                diagnostics.push_back(error("GLTF_INDEX_RANGE", "An index exceeds uint32 capacity.", path));
                return {};
            }
            source[index] = static_cast<std::uint32_t>(value);
        }
    } else {
        for (std::size_t index = 0U; index < source.size(); ++index) {
            source[index] = static_cast<std::uint32_t>(index);
        }
    }
    if (std::ranges::any_of(source, [vertex_count](const std::uint32_t value) {
        return value >= vertex_count;
    })) {
        diagnostics.push_back(error("GLTF_INDEX_BOUNDS", "An index exceeds the POSITION accessor bounds.", path));
        return {};
    }

    std::vector<std::uint32_t> triangles;
    if (primitive.type == cgltf_primitive_type_triangles) {
        if (source.size() % 3U != 0U) {
            diagnostics.push_back(error("GLTF_TRIANGLE_COUNT", "Triangle index count must be divisible by three.", path));
            return {};
        }
        return source;
    }
    if (source.size() < 3U) {
        return triangles;
    }
    triangles.reserve((source.size() - 2U) * 3U);
    if (primitive.type == cgltf_primitive_type_triangle_strip) {
        for (std::size_t index = 2U; index < source.size(); ++index) {
            if ((index & 1U) == 0U) {
                triangles.insert(triangles.end(), {source[index - 2U], source[index - 1U], source[index]});
            } else {
                triangles.insert(triangles.end(), {source[index - 1U], source[index - 2U], source[index]});
            }
        }
        return triangles;
    }
    if (primitive.type == cgltf_primitive_type_triangle_fan) {
        for (std::size_t index = 2U; index < source.size(); ++index) {
            triangles.insert(triangles.end(), {source[0U], source[index - 1U], source[index]});
        }
        return triangles;
    }
    diagnostics.push_back(error("GLTF_PRIMITIVE_MODE", "Only triangle, triangle-strip, and triangle-fan primitives produce geometry buffers.", path));
    return {};
}

bool decode_primitive(
    const cgltf_primitive& primitive,
    const cgltf_skin* skin,
    const std::unordered_map<const cgltf_node*, std::uint32_t>& global_joints,
    PrimitiveBuffers& output,
    std::vector<Diagnostic>& diagnostics,
    const std::string& path
) {
    if (primitive.has_draco_mesh_compression) {
        diagnostics.push_back(error("GLTF_DRACO_UNSUPPORTED", "Draco-compressed primitives require a decoder before buffers can be registered.", path));
        return false;
    }
    const cgltf_accessor* positions = nullptr;
    std::map<int, const cgltf_accessor*> joints;
    std::map<int, const cgltf_accessor*> weights;
    for (cgltf_size index = 0U; index < primitive.attributes_count; ++index) {
        const cgltf_attribute& attribute = primitive.attributes[index];
        if (attribute.type == cgltf_attribute_type_position && attribute.index == 0) {
            positions = attribute.data;
        } else if (attribute.type == cgltf_attribute_type_joints) {
            joints.emplace(attribute.index, attribute.data);
        } else if (attribute.type == cgltf_attribute_type_weights) {
            weights.emplace(attribute.index, attribute.data);
        }
    }
    if (positions == nullptr || positions->type != cgltf_type_vec3 || compressed_accessor(positions)) {
        diagnostics.push_back(error("GLTF_POSITION_BUFFER", "A decoded VEC3 POSITION accessor is required.", path));
        return false;
    }
    if (positions->count == 0U || positions->count > std::numeric_limits<std::uint32_t>::max()) {
        diagnostics.push_back(error("GLTF_VERTEX_COUNT", "POSITION count is empty or exceeds uint32 capacity.", path));
        return false;
    }
    std::vector<float> position_values(positions->count * 3U);
    if (cgltf_accessor_unpack_floats(positions, position_values.data(), position_values.size())
        != position_values.size()) {
        diagnostics.push_back(error("GLTF_POSITION_DECODE", "Failed to unpack the POSITION accessor.", path));
        return false;
    }
    output.positions = make_attribute(
        VertexSemantic::position,
        ScalarType::float32,
        3U,
        positions->count,
        immutable_bytes(std::move(position_values))
    );

    std::vector<std::uint32_t> indices = triangle_indices(
        primitive,
        positions->count,
        diagnostics,
        path + ".indices"
    );
    if (indices.empty()) {
        return false;
    }
    const std::uint64_t triangle_index_count = indices.size();
    output.indices = IndexBuffer{BufferView{
        .storage = immutable_bytes(std::move(indices)),
        .byte_offset = 0U,
        .byte_stride = 0U,
        .element_count = triangle_index_count,
        .component_count = 1U,
        .scalar_type = ScalarType::uint32,
    }};

    if (joints.empty() && weights.empty()) {
        return true;
    }
    if (skin == nullptr || joints.size() != weights.size() || joints.empty()) {
        diagnostics.push_back(error("GLTF_SKIN_PAIR", "Skinned primitives require a node skin and matching JOINTS_n/WEIGHTS_n accessors.", path));
        return false;
    }
    const std::size_t set_count = joints.rbegin()->first >= 0
        ? static_cast<std::size_t>(joints.rbegin()->first) + 1U
        : 0U;
    std::vector<std::vector<std::uint32_t>> joint_values(set_count);
    std::vector<std::vector<float>> weight_values(set_count);
    for (std::size_t set = 0U; set < set_count; ++set) {
        const auto joint = joints.find(static_cast<int>(set));
        const auto weight = weights.find(static_cast<int>(set));
        if (joint == joints.end() || weight == weights.end()
            || joint->second->type != cgltf_type_vec4
            || weight->second->type != cgltf_type_vec4
            || joint->second->count != positions->count
            || weight->second->count != positions->count
            || compressed_accessor(joint->second)
            || compressed_accessor(weight->second)) {
            diagnostics.push_back(error("GLTF_SKIN_ACCESSOR", "Skin accessor sets must be contiguous decoded VEC4 arrays matching POSITION count.", path));
            return false;
        }
        std::vector<float> local_joints(positions->count * 4U);
        weight_values[set].resize(positions->count * 4U);
        if (cgltf_accessor_unpack_floats(joint->second, local_joints.data(), local_joints.size()) != local_joints.size()
            || cgltf_accessor_unpack_floats(weight->second, weight_values[set].data(), weight_values[set].size()) != weight_values[set].size()) {
            diagnostics.push_back(error("GLTF_SKIN_DECODE", "Failed to unpack a JOINTS_n/WEIGHTS_n pair.", path));
            return false;
        }
        joint_values[set].resize(local_joints.size());
        for (std::size_t component = 0U; component < local_joints.size(); ++component) {
            const float local_value = local_joints[component];
            if (!std::isfinite(local_value) || local_value < 0.0F
                || std::floor(local_value) != local_value
                || local_value >= static_cast<float>(skin->joints_count)) {
                diagnostics.push_back(error("GLTF_JOINT_INDEX", "A JOINTS_n value is outside the node skin.", path));
                return false;
            }
            const auto found = global_joints.find(skin->joints[static_cast<std::size_t>(local_value)]);
            if (found == global_joints.end()) {
                diagnostics.push_back(error("GLTF_JOINT_MAP", "A skin joint could not be mapped into the asset joint table.", path));
                return false;
            }
            joint_values[set][component] = found->second;
        }
    }

    std::uint32_t maximum = 0U;
    for (std::size_t vertex = 0U; vertex < positions->count; ++vertex) {
        double sum = 0.0;
        std::uint32_t count = 0U;
        for (std::size_t set = 0U; set < set_count; ++set) {
            for (std::size_t lane = 0U; lane < 4U; ++lane) {
                float& weight = weight_values[set][vertex * 4U + lane];
                if (!std::isfinite(weight) || weight < 0.0F) {
                    diagnostics.push_back(error("GLTF_SKIN_WEIGHT", "Skin weights must be finite and non-negative.", path));
                    return false;
                }
                if (weight > 0.0F) {
                    sum += weight;
                    ++count;
                }
            }
        }
        if (sum <= 0.0) {
            diagnostics.push_back(error("GLTF_ZERO_SKIN_WEIGHT", "Every skinned vertex requires a positive total weight.", path));
            return false;
        }
        for (std::size_t set = 0U; set < set_count; ++set) {
            for (std::size_t lane = 0U; lane < 4U; ++lane) {
                weight_values[set][vertex * 4U + lane] = static_cast<float>(
                    static_cast<double>(weight_values[set][vertex * 4U + lane]) / sum
                );
            }
        }
        maximum = std::max(maximum, count);
    }
    output.max_influences = maximum;
    output.influence_sets.reserve(set_count);
    for (std::size_t set = 0U; set < set_count; ++set) {
        output.influence_sets.push_back(SkinInfluenceSet{
            .joints = make_attribute(VertexSemantic::joints, ScalarType::uint32, 4U, positions->count, immutable_bytes(std::move(joint_values[set]))),
            .weights = make_attribute(VertexSemantic::weights, ScalarType::float32, 4U, positions->count, immutable_bytes(std::move(weight_values[set]))),
        });
    }
    return true;
}

Matrix4d world_matrix(const cgltf_node* node) {
    cgltf_float source[16]{};
    cgltf_node_transform_world(node, source);
    Matrix4d result{};
    std::ranges::transform(source, source + 16, result.begin(), [](const float value) {
        return static_cast<double>(value);
    });
    return result;
}

}  // namespace

DecodeResult decode_cgltf(const std::filesystem::path& path) {
    cgltf_options options{};
    cgltf_data* raw = nullptr;
    const std::string filename = utf8_path(path);
    cgltf_result status = cgltf_parse_file(&options, filename.c_str(), &raw);
    if (status != cgltf_result_success) {
        return {
            .asset = std::nullopt,
            .diagnostics = {error("GLTF_PARSE_FAILED", std::string{"cgltf parse failed: "} + result_name(status))},
        };
    }
    const std::unique_ptr<cgltf_data, decltype(&cgltf_free)> data{raw, &cgltf_free};
    status = cgltf_load_buffers(&options, data.get(), filename.c_str());
    if (status != cgltf_result_success) {
        return {
            .asset = std::nullopt,
            .diagnostics = {error("GLTF_BUFFER_LOAD_FAILED", std::string{"cgltf buffer loading failed: "} + result_name(status))},
        };
    }
    status = cgltf_validate(data.get());
    if (status != cgltf_result_success) {
        return {
            .asset = std::nullopt,
            .diagnostics = {error("GLTF_VALIDATION_FAILED", std::string{"cgltf validation failed: "} + result_name(status))},
        };
    }

    DecodedAssetBuffers decoded;
    decoded.adapter = "cgltf/1.15";
    std::unordered_map<const cgltf_node*, std::uint32_t> global_joints;
    for (cgltf_size skin_index = 0U; skin_index < data->skins_count; ++skin_index) {
        const cgltf_skin& skin = data->skins[skin_index];
        for (cgltf_size joint_index = 0U; joint_index < skin.joints_count; ++joint_index) {
            const cgltf_node* joint = skin.joints[joint_index];
            if (!global_joints.contains(joint)) {
                const auto id = static_cast<std::uint32_t>(decoded.joint_names.size());
                global_joints.emplace(joint, id);
                decoded.joint_names.emplace_back(joint != nullptr && joint->name != nullptr
                    ? joint->name
                    : "joint_" + std::to_string(id));
            }
        }
    }

    std::vector<bool> referenced(data->meshes_count, false);
    const auto emit_mesh = [&](const cgltf_mesh& mesh, const cgltf_node* node, DecodedAssetBuffers& target, std::vector<Diagnostic>& diagnostics) {
        const auto mesh_index = static_cast<std::uint64_t>(&mesh - data->meshes);
        referenced[static_cast<std::size_t>(mesh_index)] = true;
        for (cgltf_size primitive_index = 0U; primitive_index < mesh.primitives_count; ++primitive_index) {
            DecodedPrimitive decoded_primitive{
                .name = node != nullptr && node->name != nullptr
                    ? node->name
                    : mesh.name != nullptr ? mesh.name : "mesh_" + std::to_string(mesh_index),
                .source_mesh_index = mesh_index,
                .source_primitive_index = primitive_index,
                .domain = GeometryDomain::render_vertices,
                .local_to_world = node != nullptr ? world_matrix(node) : Matrix4d{
                    1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0,
                    0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0,
                },
                .buffers = {},
            };
            const std::string diagnostic_path = "$.meshes[" + std::to_string(mesh_index)
                + "].primitives[" + std::to_string(primitive_index) + "]";
            if (decode_primitive(mesh.primitives[primitive_index], node != nullptr ? node->skin : nullptr,
                global_joints, decoded_primitive.buffers, diagnostics, diagnostic_path)) {
                const ValidationResult validation = geometry::validate_primitive_buffers(decoded_primitive.buffers);
                diagnostics.insert(diagnostics.end(), validation.diagnostics.begin(), validation.diagnostics.end());
                if (validation.valid()) {
                    target.primitives.push_back(std::move(decoded_primitive));
                }
            }
        }
    };

    std::vector<Diagnostic> diagnostics;
    for (cgltf_size node_index = 0U; node_index < data->nodes_count; ++node_index) {
        const cgltf_node& node = data->nodes[node_index];
        if (node.mesh != nullptr) {
            emit_mesh(*node.mesh, &node, decoded, diagnostics);
        }
    }
    for (cgltf_size mesh_index = 0U; mesh_index < data->meshes_count; ++mesh_index) {
        if (!referenced[mesh_index]) {
            emit_mesh(data->meshes[mesh_index], nullptr, decoded, diagnostics);
        }
    }
    if (decoded.primitives.empty()) {
        diagnostics.push_back(error("GLTF_NO_TRIANGLE_BUFFERS", "The asset did not produce any valid triangle buffers."));
    }
    const bool failed = std::ranges::any_of(diagnostics, [](const Diagnostic& item) {
        return item.severity == DiagnosticSeverity::error;
    });
    return {
        .asset = failed ? std::nullopt : std::optional<DecodedAssetBuffers>{std::move(decoded)},
        .diagnostics = std::move(diagnostics),
    };
}

}  // namespace unified3d::adapters::detail
