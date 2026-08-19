#include "../adapter_internal.hpp"

#include <ufbx.h>

#include <algorithm>
#include <array>
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

std::string value(const ufbx_string string) {
    return string.data != nullptr ? std::string{string.data, string.length} : std::string{};
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

Matrix4d matrix(const ufbx_matrix& source) {
    return {
        static_cast<double>(source.m00), static_cast<double>(source.m10), static_cast<double>(source.m20), 0.0,
        static_cast<double>(source.m01), static_cast<double>(source.m11), static_cast<double>(source.m21), 0.0,
        static_cast<double>(source.m02), static_cast<double>(source.m12), static_cast<double>(source.m22), 0.0,
        static_cast<double>(source.m03), static_cast<double>(source.m13), static_cast<double>(source.m23), 1.0,
    };
}

bool decode_mesh(
    const ufbx_mesh& mesh,
    const std::unordered_map<const ufbx_node*, std::uint32_t>& global_joints,
    PrimitiveBuffers& output,
    std::vector<Diagnostic>& diagnostics,
    const std::string& path
) {
    if (mesh.num_vertices == 0U || mesh.num_vertices > std::numeric_limits<std::uint32_t>::max()) {
        diagnostics.push_back(error("FBX_VERTEX_COUNT", "FBX control-point count is empty or exceeds uint32 capacity.", path));
        return false;
    }
    std::vector<double> positions;
    positions.reserve(mesh.num_vertices * 3U);
    for (std::size_t index = 0U; index < mesh.num_vertices; ++index) {
        const ufbx_vec3 position = mesh.vertices.data[index];
        positions.insert(positions.end(), {
            static_cast<double>(position.x),
            static_cast<double>(position.y),
            static_cast<double>(position.z),
        });
    }
    output.positions = make_attribute(
        VertexSemantic::position,
        ScalarType::float64,
        3U,
        mesh.num_vertices,
        immutable_bytes(std::move(positions))
    );

    std::vector<std::uint32_t> indices;
    if (mesh.num_triangles > std::numeric_limits<std::size_t>::max() / 3U) {
        diagnostics.push_back(error("FBX_TRIANGLE_COUNT", "FBX triangle count exceeds addressable memory.", path));
        return false;
    }
    indices.reserve(mesh.num_triangles * 3U);
    std::vector<std::uint32_t> face_indices(std::max<std::size_t>(mesh.max_face_triangles * 3U, 3U));
    for (std::size_t face_index = 0U; face_index < mesh.faces.count; ++face_index) {
        const ufbx_face face = mesh.faces.data[face_index];
        const std::size_t triangle_count = ufbx_triangulate_face(
            face_indices.data(), face_indices.size(), &mesh, face
        );
        for (std::size_t index = 0U; index < triangle_count * 3U; ++index) {
            const std::uint32_t polygon_vertex = face_indices[index];
            if (polygon_vertex >= mesh.vertex_indices.count) {
                diagnostics.push_back(error("FBX_POLYGON_INDEX", "Triangulation produced an invalid polygon-vertex index.", path));
                return false;
            }
            const std::uint32_t control_point = mesh.vertex_indices.data[polygon_vertex];
            if (control_point >= mesh.num_vertices) {
                diagnostics.push_back(error("FBX_CONTROL_POINT_INDEX", "A triangle references an invalid FBX control point.", path));
                return false;
            }
            indices.push_back(control_point);
        }
    }
    if (indices.empty()) {
        diagnostics.push_back(error("FBX_NO_TRIANGLES", "The FBX mesh did not produce triangle indices.", path));
        return false;
    }
    output.indices = IndexBuffer{BufferView{
        .storage = immutable_bytes(std::move(indices)),
        .byte_offset = 0U,
        .byte_stride = 0U,
        .element_count = mesh.num_triangles * 3U,
        .component_count = 1U,
        .scalar_type = ScalarType::uint32,
    }};

    if (mesh.skin_deformers.count == 0U) {
        return true;
    }
    std::vector<std::map<std::uint32_t, double>> influences(mesh.num_vertices);
    for (std::size_t deformer_index = 0U; deformer_index < mesh.skin_deformers.count; ++deformer_index) {
        const ufbx_skin_deformer& skin = *mesh.skin_deformers.data[deformer_index];
        if (skin.vertices.count < mesh.num_vertices) {
            diagnostics.push_back(error("FBX_SKIN_VERTEX_COUNT", "A skin deformer does not cover every FBX control point.", path));
            return false;
        }
        for (std::size_t vertex_index = 0U; vertex_index < mesh.num_vertices; ++vertex_index) {
            const ufbx_skin_vertex vertex = skin.vertices.data[vertex_index];
            const std::size_t end = static_cast<std::size_t>(vertex.weight_begin)
                + static_cast<std::size_t>(vertex.num_weights);
            if (end > skin.weights.count) {
                diagnostics.push_back(error("FBX_SKIN_WEIGHT_RANGE", "A skin vertex references weights outside the deformer.", path));
                return false;
            }
            for (std::size_t weight_index = vertex.weight_begin; weight_index < end; ++weight_index) {
                const ufbx_skin_weight weight = skin.weights.data[weight_index];
                if (weight.cluster_index >= skin.clusters.count || !std::isfinite(static_cast<double>(weight.weight))
                    || weight.weight < 0.0) {
                    diagnostics.push_back(error("FBX_SKIN_WEIGHT", "A skin weight or cluster index is invalid.", path));
                    return false;
                }
                const ufbx_skin_cluster& cluster = *skin.clusters.data[weight.cluster_index];
                const auto joint = global_joints.find(cluster.bone_node);
                if (joint == global_joints.end()) {
                    diagnostics.push_back(error("FBX_JOINT_MAP", "A skin cluster could not be mapped into the asset joint table.", path));
                    return false;
                }
                influences[vertex_index][joint->second] += static_cast<double>(weight.weight);
            }
        }
    }

    std::vector<std::vector<std::pair<std::uint32_t, float>>> normalized(mesh.num_vertices);
    std::uint32_t maximum = 0U;
    for (std::size_t vertex_index = 0U; vertex_index < influences.size(); ++vertex_index) {
        double sum = 0.0;
        for (const auto& [joint, weight] : influences[vertex_index]) {
            static_cast<void>(joint);
            if (weight > 0.0) {
                sum += weight;
            }
        }
        if (sum <= 0.0) {
            continue;
        }
        auto& target = normalized[vertex_index];
        target.reserve(influences[vertex_index].size());
        for (const auto& [joint, weight] : influences[vertex_index]) {
            if (weight > 0.0) {
                target.emplace_back(joint, static_cast<float>(weight / sum));
            }
        }
        std::ranges::sort(target, [](const auto& left, const auto& right) {
            return left.second > right.second;
        });
        maximum = std::max(maximum, static_cast<std::uint32_t>(target.size()));
    }
    if (maximum == 0U) {
        diagnostics.push_back(error("FBX_ZERO_SKIN", "FBX skin deformers contain no positive control-point weights.", path));
        return false;
    }
    output.max_influences = maximum;
    const std::size_t set_count = (static_cast<std::size_t>(maximum) + 3U) / 4U;
    output.influence_sets.reserve(set_count);
    for (std::size_t set = 0U; set < set_count; ++set) {
        std::vector<std::uint32_t> joints(mesh.num_vertices * 4U, 0U);
        std::vector<float> weights(mesh.num_vertices * 4U, 0.0F);
        for (std::size_t vertex_index = 0U; vertex_index < normalized.size(); ++vertex_index) {
            for (std::size_t lane = 0U; lane < 4U; ++lane) {
                const std::size_t influence = set * 4U + lane;
                if (influence < normalized[vertex_index].size()) {
                    joints[vertex_index * 4U + lane] = normalized[vertex_index][influence].first;
                    weights[vertex_index * 4U + lane] = normalized[vertex_index][influence].second;
                }
            }
        }
        output.influence_sets.push_back(SkinInfluenceSet{
            .joints = make_attribute(VertexSemantic::joints, ScalarType::uint32, 4U, mesh.num_vertices, immutable_bytes(std::move(joints))),
            .weights = make_attribute(VertexSemantic::weights, ScalarType::float32, 4U, mesh.num_vertices, immutable_bytes(std::move(weights))),
        });
    }
    return true;
}

}  // namespace

DecodeResult decode_ufbx(const std::filesystem::path& path) {
    const std::string filename = utf8_path(path);
    ufbx_error load_error{};
    ufbx_load_opts options{};
    options.target_axes = ufbx_axes_right_handed_y_up;
    options.target_unit_meters = 1.0;
    options.space_conversion = UFBX_SPACE_CONVERSION_TRANSFORM_ROOT;
    ufbx_scene* raw = ufbx_load_file(filename.c_str(), &options, &load_error);
    if (raw == nullptr) {
        std::array<char, 1024> formatted{};
        ufbx_format_error(formatted.data(), formatted.size(), &load_error);
        return {
            .asset = std::nullopt,
            .diagnostics = {error("FBX_PARSE_FAILED", formatted.data())},
        };
    }
    const std::unique_ptr<ufbx_scene, decltype(&ufbx_free_scene)> scene{raw, &ufbx_free_scene};
    DecodedAssetBuffers decoded;
    decoded.adapter = "ufbx/0.23.0";
    std::unordered_map<const ufbx_node*, std::uint32_t> global_joints;
    for (std::size_t cluster_index = 0U; cluster_index < scene->skin_clusters.count; ++cluster_index) {
        const ufbx_node* node = scene->skin_clusters.data[cluster_index]->bone_node;
        if (node != nullptr && !global_joints.contains(node)) {
            const auto id = static_cast<std::uint32_t>(decoded.joint_names.size());
            global_joints.emplace(node, id);
            std::string name = value(node->name);
            decoded.joint_names.push_back(name.empty() ? "joint_" + std::to_string(id) : std::move(name));
        }
    }

    std::vector<Diagnostic> diagnostics;
    for (std::size_t mesh_index = 0U; mesh_index < scene->meshes.count; ++mesh_index) {
        const ufbx_mesh& mesh = *scene->meshes.data[mesh_index];
        PrimitiveBuffers buffers;
        const std::string diagnostic_path = "$.meshes[" + std::to_string(mesh_index) + "]";
        if (!decode_mesh(mesh, global_joints, buffers, diagnostics, diagnostic_path)) {
            continue;
        }
        const ValidationResult validation = geometry::validate_primitive_buffers(buffers);
        diagnostics.insert(diagnostics.end(), validation.diagnostics.begin(), validation.diagnostics.end());
        if (!validation.valid()) {
            continue;
        }
        if (mesh.instances.count == 0U) {
            decoded.primitives.push_back(DecodedPrimitive{
                .name = value(mesh.name),
                .source_mesh_index = mesh_index,
                .source_primitive_index = 0U,
                .domain = GeometryDomain::geometric_vertices,
                .buffers = buffers,
            });
        } else {
            for (std::size_t instance_index = 0U; instance_index < mesh.instances.count; ++instance_index) {
                const ufbx_node& node = *mesh.instances.data[instance_index];
                std::string name = value(node.name);
                decoded.primitives.push_back(DecodedPrimitive{
                    .name = name.empty() ? value(mesh.name) : std::move(name),
                    .source_mesh_index = mesh_index,
                    .source_primitive_index = instance_index,
                    .domain = GeometryDomain::geometric_vertices,
                    .local_to_world = matrix(node.geometry_to_world),
                    .buffers = buffers,
                });
            }
        }
    }
    if (decoded.primitives.empty()) {
        diagnostics.push_back(error("FBX_NO_TRIANGLE_BUFFERS", "The asset did not produce any valid triangle buffers."));
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
