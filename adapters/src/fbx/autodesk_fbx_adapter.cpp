#include "../adapter_internal.hpp"

#include <fbxsdk.h>

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

struct FbxDestroy {
    template <typename T>
    void operator()(T* value) const noexcept {
        if (value != nullptr) {
            value->Destroy();
        }
    }
};

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

Matrix4d matrix(const FbxAMatrix& source) {
    Matrix4d result{};
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            result[static_cast<std::size_t>(column * 4 + row)] = source.Get(row, column);
        }
    }
    return result;
}

FbxAMatrix geometry_to_world(FbxNode& node) {
    FbxAMatrix geometry;
    geometry.SetT(node.GetGeometricTranslation(FbxNode::eSourcePivot));
    geometry.SetR(node.GetGeometricRotation(FbxNode::eSourcePivot));
    geometry.SetS(node.GetGeometricScaling(FbxNode::eSourcePivot));
    return node.EvaluateGlobalTransform() * geometry;
}

void collect_mesh_nodes(FbxNode* node, std::vector<FbxNode*>& output) {
    if (node == nullptr) {
        return;
    }
    if (node->GetMesh() != nullptr) {
        output.push_back(node);
    }
    for (int index = 0; index < node->GetChildCount(); ++index) {
        collect_mesh_nodes(node->GetChild(index), output);
    }
}

bool decode_mesh(
    FbxMesh& mesh,
    const std::unordered_map<const FbxNode*, std::uint32_t>& global_joints,
    PrimitiveBuffers& output,
    std::vector<Diagnostic>& diagnostics,
    const std::string& path
) {
    const int control_point_count = mesh.GetControlPointsCount();
    if (control_point_count <= 0) {
        diagnostics.push_back(error("FBX_VERTEX_COUNT", "Autodesk FBX mesh contains no control points.", path));
        return false;
    }
    std::vector<double> positions;
    positions.reserve(static_cast<std::size_t>(control_point_count) * 3U);
    const FbxVector4* control_points = mesh.GetControlPoints();
    for (int index = 0; index < control_point_count; ++index) {
        positions.insert(positions.end(), {
            control_points[index][0], control_points[index][1], control_points[index][2],
        });
    }
    output.positions = make_attribute(
        VertexSemantic::position,
        ScalarType::float64,
        3U,
        static_cast<std::uint64_t>(control_point_count),
        immutable_bytes(std::move(positions))
    );

    std::vector<std::uint32_t> indices;
    const int polygon_count = mesh.GetPolygonCount();
    indices.reserve(static_cast<std::size_t>(polygon_count) * 3U);
    for (int polygon = 0; polygon < polygon_count; ++polygon) {
        if (mesh.GetPolygonSize(polygon) != 3) {
            diagnostics.push_back(error("FBX_TRIANGULATION", "Autodesk FBX geometry converter left a non-triangle polygon.", path));
            return false;
        }
        for (int lane = 0; lane < 3; ++lane) {
            const int control_point = mesh.GetPolygonVertex(polygon, lane);
            if (control_point < 0 || control_point >= control_point_count) {
                diagnostics.push_back(error("FBX_CONTROL_POINT_INDEX", "A triangle references an invalid FBX control point.", path));
                return false;
            }
            indices.push_back(static_cast<std::uint32_t>(control_point));
        }
    }
    if (indices.empty()) {
        diagnostics.push_back(error("FBX_NO_TRIANGLES", "The Autodesk FBX mesh did not produce triangle indices.", path));
        return false;
    }
    output.indices = IndexBuffer{BufferView{
        .storage = immutable_bytes(std::move(indices)),
        .byte_offset = 0U,
        .byte_stride = 0U,
        .element_count = static_cast<std::uint64_t>(polygon_count) * 3U,
        .component_count = 1U,
        .scalar_type = ScalarType::uint32,
    }};

    const int skin_count = mesh.GetDeformerCount(FbxDeformer::eSkin);
    if (skin_count == 0) {
        return true;
    }
    std::vector<std::map<std::uint32_t, double>> influences(
        static_cast<std::size_t>(control_point_count)
    );
    for (int skin_index = 0; skin_index < skin_count; ++skin_index) {
        const auto* skin = static_cast<const FbxSkin*>(
            mesh.GetDeformer(skin_index, FbxDeformer::eSkin)
        );
        for (int cluster_index = 0; cluster_index < skin->GetClusterCount(); ++cluster_index) {
            const FbxCluster* cluster = skin->GetCluster(cluster_index);
            const auto joint = global_joints.find(cluster->GetLink());
            if (joint == global_joints.end()) {
                diagnostics.push_back(error("FBX_JOINT_MAP", "An Autodesk FBX cluster could not be mapped into the asset joint table.", path));
                return false;
            }
            const int weight_count = cluster->GetControlPointIndicesCount();
            const int* vertex_indices = cluster->GetControlPointIndices();
            const double* weights = cluster->GetControlPointWeights();
            for (int weight_index = 0; weight_index < weight_count; ++weight_index) {
                const int vertex = vertex_indices[weight_index];
                const double weight = weights[weight_index];
                if (vertex < 0 || vertex >= control_point_count || !std::isfinite(weight) || weight < 0.0) {
                    diagnostics.push_back(error("FBX_SKIN_WEIGHT", "An Autodesk FBX cluster weight is invalid.", path));
                    return false;
                }
                influences[static_cast<std::size_t>(vertex)][joint->second] += weight;
            }
        }
    }

    std::vector<std::vector<std::pair<std::uint32_t, float>>> normalized(
        static_cast<std::size_t>(control_point_count)
    );
    std::uint32_t maximum = 0U;
    for (std::size_t vertex = 0U; vertex < influences.size(); ++vertex) {
        double sum = 0.0;
        for (const auto& [joint, weight] : influences[vertex]) {
            static_cast<void>(joint);
            if (weight > 0.0) {
                sum += weight;
            }
        }
        if (sum <= 0.0) {
            continue;
        }
        for (const auto& [joint, weight] : influences[vertex]) {
            if (weight > 0.0) {
                normalized[vertex].emplace_back(joint, static_cast<float>(weight / sum));
            }
        }
        std::ranges::sort(normalized[vertex], [](const auto& left, const auto& right) {
            return left.second > right.second;
        });
        maximum = std::max(maximum, static_cast<std::uint32_t>(normalized[vertex].size()));
    }
    if (maximum == 0U) {
        diagnostics.push_back(error("FBX_ZERO_SKIN", "Autodesk FBX skin deformers contain no positive weights.", path));
        return false;
    }
    output.max_influences = maximum;
    const std::size_t set_count = (static_cast<std::size_t>(maximum) + 3U) / 4U;
    output.influence_sets.reserve(set_count);
    for (std::size_t set = 0U; set < set_count; ++set) {
        std::vector<std::uint32_t> joints(static_cast<std::size_t>(control_point_count) * 4U, 0U);
        std::vector<float> weights(static_cast<std::size_t>(control_point_count) * 4U, 0.0F);
        for (std::size_t vertex = 0U; vertex < normalized.size(); ++vertex) {
            for (std::size_t lane = 0U; lane < 4U; ++lane) {
                const std::size_t influence = set * 4U + lane;
                if (influence < normalized[vertex].size()) {
                    joints[vertex * 4U + lane] = normalized[vertex][influence].first;
                    weights[vertex * 4U + lane] = normalized[vertex][influence].second;
                }
            }
        }
        output.influence_sets.push_back(SkinInfluenceSet{
            .joints = make_attribute(VertexSemantic::joints, ScalarType::uint32, 4U, static_cast<std::uint64_t>(control_point_count), immutable_bytes(std::move(joints))),
            .weights = make_attribute(VertexSemantic::weights, ScalarType::float32, 4U, static_cast<std::uint64_t>(control_point_count), immutable_bytes(std::move(weights))),
        });
    }
    return true;
}

}  // namespace

DecodeResult decode_autodesk_fbx(const std::filesystem::path& path) {
    std::unique_ptr<FbxManager, FbxDestroy> manager{FbxManager::Create()};
    if (!manager) {
        return {
            .asset = std::nullopt,
            .diagnostics = {error("FBX_SDK_MANAGER", "Autodesk FBX SDK manager creation failed.")},
        };
    }
    FbxIOSettings* io = FbxIOSettings::Create(manager.get(), IOSROOT);
    manager->SetIOSettings(io);
    std::unique_ptr<FbxImporter, FbxDestroy> importer{FbxImporter::Create(manager.get(), "")};
    std::unique_ptr<FbxScene, FbxDestroy> scene{FbxScene::Create(manager.get(), "Unified3D")};
    const std::string filename = path.string();
    if (!importer->Initialize(filename.c_str(), -1, manager->GetIOSettings())) {
        return {
            .asset = std::nullopt,
            .diagnostics = {error("FBX_IMPORT_INITIALIZE", importer->GetStatus().GetErrorString())},
        };
    }
    if (!importer->Import(scene.get())) {
        return {
            .asset = std::nullopt,
            .diagnostics = {error("FBX_IMPORT_FAILED", importer->GetStatus().GetErrorString())},
        };
    }
    importer.reset();
    FbxAxisSystem::OpenGL.ConvertScene(scene.get());
    FbxSystemUnit::m.ConvertScene(scene.get());
    FbxGeometryConverter converter{manager.get()};
    if (!converter.Triangulate(scene.get(), true, false)) {
        return {
            .asset = std::nullopt,
            .diagnostics = {error("FBX_TRIANGULATION", "Autodesk FBX SDK failed to triangulate the scene.")},
        };
    }

    std::vector<FbxNode*> mesh_nodes;
    collect_mesh_nodes(scene->GetRootNode(), mesh_nodes);
    DecodedAssetBuffers decoded;
    decoded.adapter = "Autodesk FBX SDK/2020.3.10";
    std::unordered_map<const FbxNode*, std::uint32_t> global_joints;
    for (FbxNode* node : mesh_nodes) {
        FbxMesh* mesh = node->GetMesh();
        const int skin_count = mesh->GetDeformerCount(FbxDeformer::eSkin);
        for (int skin_index = 0; skin_index < skin_count; ++skin_index) {
            const auto* skin = static_cast<const FbxSkin*>(mesh->GetDeformer(skin_index, FbxDeformer::eSkin));
            for (int cluster_index = 0; cluster_index < skin->GetClusterCount(); ++cluster_index) {
                const FbxNode* joint = skin->GetCluster(cluster_index)->GetLink();
                if (joint != nullptr && !global_joints.contains(joint)) {
                    const auto id = static_cast<std::uint32_t>(decoded.joint_names.size());
                    global_joints.emplace(joint, id);
                    const char* name = joint->GetName();
                    decoded.joint_names.emplace_back(name != nullptr && *name != '\0'
                        ? name
                        : "joint_" + std::to_string(id));
                }
            }
        }
    }

    std::vector<Diagnostic> diagnostics;
    for (std::size_t mesh_index = 0U; mesh_index < mesh_nodes.size(); ++mesh_index) {
        FbxNode& node = *mesh_nodes[mesh_index];
        PrimitiveBuffers buffers;
        const std::string diagnostic_path = "$.meshes[" + std::to_string(mesh_index) + "]";
        if (!decode_mesh(*node.GetMesh(), global_joints, buffers, diagnostics, diagnostic_path)) {
            continue;
        }
        const ValidationResult validation = geometry::validate_primitive_buffers(buffers);
        diagnostics.insert(diagnostics.end(), validation.diagnostics.begin(), validation.diagnostics.end());
        if (validation.valid()) {
            decoded.primitives.push_back(DecodedPrimitive{
                .name = node.GetName(),
                .source_mesh_index = mesh_index,
                .source_primitive_index = 0U,
                .domain = GeometryDomain::geometric_vertices,
                .local_to_world = matrix(geometry_to_world(node)),
                .buffers = std::move(buffers),
            });
        }
    }
    if (decoded.primitives.empty()) {
        diagnostics.push_back(error("FBX_NO_TRIANGLE_BUFFERS", "The Autodesk adapter did not produce any valid triangle buffers."));
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
