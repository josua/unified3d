#include <unified3d/adapters/glb_to_fbx_converter.hpp>

#include <cgltf.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <ranges>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(UNIFIED3D_HAS_AUTODESK_FBX)
#include <fbxsdk.h>
#endif

namespace unified3d::adapters {
namespace {

Diagnostic conversion_error(
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

std::string utf8_path(const std::filesystem::path& path) {
#if defined(_WIN32)
    const std::u8string value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
#else
    return path.string();
#endif
}

#if defined(UNIFIED3D_HAS_AUTODESK_FBX)

template <typename T>
struct FbxDestroy final {
    void operator()(T* value) const noexcept {
        if (value != nullptr) {
            value->Destroy();
        }
    }
};

struct CgltfDestroy final {
    void operator()(cgltf_data* value) const noexcept {
        if (value != nullptr) {
            cgltf_free(value);
        }
    }
};

struct ExtractedMedia final {
    std::filesystem::path directory;
    std::vector<std::filesystem::path> files;

    ~ExtractedMedia() {
        std::error_code ignored;
        for (const auto& file : files) {
            std::filesystem::remove(file, ignored);
            ignored.clear();
        }
        if (!directory.empty()) {
            std::filesystem::remove(directory, ignored);
        }
    }
};

std::string sanitize_name(std::string value, const std::string& fallback) {
    if (value.empty()) {
        value = fallback;
    }
    for (char& character : value) {
        const unsigned char byte = static_cast<unsigned char>(character);
        if (!(std::isalnum(byte) != 0 || character == '-' || character == '_')) {
            character = '_';
        }
    }
    return value;
}

std::string image_extension(const cgltf_image& image) {
    const std::string mime = image.mime_type != nullptr ? image.mime_type : "";
    if (mime == "image/jpeg") {
        return ".jpg";
    }
    if (mime == "image/png") {
        return ".png";
    }
    if (mime == "image/webp") {
        return ".webp";
    }
    if (mime == "image/ktx2") {
        return ".ktx2";
    }
    return ".bin";
}

bool write_embedded_image(
    const cgltf_image& image,
    const std::filesystem::path& source_path,
    const std::filesystem::path& destination,
    std::vector<Diagnostic>& diagnostics,
    const std::string& diagnostic_path
) {
    if (image.buffer_view != nullptr) {
        const cgltf_buffer_view& view = *image.buffer_view;
        if (view.buffer == nullptr || view.buffer->data == nullptr
            || view.offset > view.buffer->size
            || view.size > view.buffer->size - view.offset) {
            diagnostics.push_back(conversion_error(
                "GLB_IMAGE_BUFFER",
                "An embedded image buffer view is outside the loaded GLB buffer.",
                diagnostic_path
            ));
            return false;
        }
        const auto* bytes = static_cast<const std::byte*>(view.buffer->data) + view.offset;
        std::ofstream stream(destination, std::ios::binary | std::ios::trunc);
        if (!stream) {
            diagnostics.push_back(conversion_error(
                "FBX_MEDIA_WRITE",
                "Failed to create a temporary media file for FBX embedding.",
                diagnostic_path
            ));
            return false;
        }
        stream.write(reinterpret_cast<const char*>(bytes), static_cast<std::streamsize>(view.size));
        if (!stream) {
            diagnostics.push_back(conversion_error(
                "FBX_MEDIA_WRITE",
                "Failed while writing a temporary media file for FBX embedding.",
                diagnostic_path
            ));
            return false;
        }
        return true;
    }

    if (image.uri == nullptr || *image.uri == '\0') {
        diagnostics.push_back(conversion_error(
            "GLB_IMAGE_SOURCE",
            "An image has neither an embedded buffer view nor an external URI.",
            diagnostic_path
        ));
        return false;
    }
    const std::string uri = image.uri;
    if (uri.starts_with("data:")) {
        diagnostics.push_back(conversion_error(
            "GLTF_DATA_URI_UNSUPPORTED",
            "Data URI images are not supported by the targeted GLB-to-FBX converter.",
            diagnostic_path
        ));
        return false;
    }
    const std::filesystem::path external = source_path.parent_path() / std::filesystem::path{uri};
    std::error_code error;
    std::filesystem::copy_file(
        external,
        destination,
        std::filesystem::copy_options::overwrite_existing,
        error
    );
    if (error) {
        diagnostics.push_back(conversion_error(
            "GLTF_EXTERNAL_IMAGE",
            "Failed to stage an external image for FBX embedding: " + error.message(),
            diagnostic_path
        ));
        return false;
    }
    return true;
}

const cgltf_accessor* find_attribute(
    const cgltf_primitive& primitive,
    const cgltf_attribute_type type,
    const int set_index = 0
) {
    for (cgltf_size index = 0; index < primitive.attributes_count; ++index) {
        const cgltf_attribute& attribute = primitive.attributes[index];
        if (attribute.type == type && attribute.index == set_index) {
            return attribute.data;
        }
    }
    return nullptr;
}

bool unpack_floats(
    const cgltf_accessor* accessor,
    const cgltf_type expected_type,
    const std::size_t component_count,
    std::vector<float>& values,
    std::vector<Diagnostic>& diagnostics,
    const std::string& path
) {
    if (accessor == nullptr || accessor->type != expected_type) {
        diagnostics.push_back(conversion_error(
            "GLB_ATTRIBUTE_TYPE",
            "A required glTF vertex attribute is absent or has the wrong type.",
            path
        ));
        return false;
    }
    if (accessor->count > std::numeric_limits<int>::max()) {
        diagnostics.push_back(conversion_error(
            "FBX_CONTROL_POINT_RANGE",
            "A primitive exceeds the Autodesk FBX SDK control-point capacity.",
            path
        ));
        return false;
    }
    values.resize(static_cast<std::size_t>(accessor->count) * component_count);
    if (cgltf_accessor_unpack_floats(accessor, values.data(), values.size()) != values.size()) {
        diagnostics.push_back(conversion_error(
            "GLB_ATTRIBUTE_DECODE",
            "Failed to unpack a glTF vertex attribute.",
            path
        ));
        return false;
    }
    return true;
}

bool read_triangle_indices(
    const cgltf_primitive& primitive,
    const cgltf_size vertex_count,
    std::vector<std::uint32_t>& triangles,
    std::vector<Diagnostic>& diagnostics,
    const std::string& path
) {
    if (primitive.type != cgltf_primitive_type_triangles) {
        diagnostics.push_back(conversion_error(
            "GLB_PRIMITIVE_MODE",
            "The lossless converter currently requires triangle-list primitives.",
            path
        ));
        return false;
    }
    const cgltf_size index_count = primitive.indices != nullptr
        ? primitive.indices->count
        : vertex_count;
    if (index_count % 3 != 0 || index_count > std::numeric_limits<std::uint32_t>::max()) {
        diagnostics.push_back(conversion_error(
            "GLB_TRIANGLE_INDICES",
            "A triangle-list primitive has an invalid index count.",
            path
        ));
        return false;
    }
    triangles.resize(static_cast<std::size_t>(index_count));
    for (cgltf_size index = 0; index < index_count; ++index) {
        const cgltf_size value = primitive.indices != nullptr
            ? cgltf_accessor_read_index(primitive.indices, index)
            : index;
        if (value >= vertex_count || value > std::numeric_limits<std::uint32_t>::max()) {
            diagnostics.push_back(conversion_error(
                "GLB_INDEX_BOUNDS",
                "A primitive index exceeds its POSITION accessor.",
                path
            ));
            return false;
        }
        triangles[static_cast<std::size_t>(index)] = static_cast<std::uint32_t>(value);
    }
    return true;
}

FbxVector4 euler_from_quaternion(const cgltf_float rotation[4]) {
    FbxAMatrix matrix;
    matrix.SetTQS(
        FbxVector4(0.0, 0.0, 0.0),
        FbxQuaternion(rotation[0], rotation[1], rotation[2], rotation[3]),
        FbxVector4(1.0, 1.0, 1.0)
    );
    return matrix.GetR();
}

void apply_node_transform(const cgltf_node& source, FbxNode& target) {
    if (!source.has_matrix) {
        if (source.has_translation) {
            target.LclTranslation.Set(FbxVector4(
                source.translation[0], source.translation[1], source.translation[2]
            ));
        }
        if (source.has_rotation) {
            target.LclRotation.Set(euler_from_quaternion(source.rotation));
        }
        if (source.has_scale) {
            target.LclScaling.Set(FbxVector4(
                source.scale[0], source.scale[1], source.scale[2]
            ));
        }
        return;
    }

    const cgltf_float* matrix = source.matrix;
    const double sx = std::hypot(matrix[0], matrix[1], matrix[2]);
    const double sy = std::hypot(matrix[4], matrix[5], matrix[6]);
    const double sz = std::hypot(matrix[8], matrix[9], matrix[10]);
    target.LclTranslation.Set(FbxVector4(matrix[12], matrix[13], matrix[14]));
    target.LclScaling.Set(FbxVector4(sx, sy, sz));
    if (sx <= 0.0 || sy <= 0.0 || sz <= 0.0) {
        return;
    }

    const double r00 = matrix[0] / sx;
    const double r10 = matrix[1] / sx;
    const double r20 = matrix[2] / sx;
    const double r01 = matrix[4] / sy;
    const double r11 = matrix[5] / sy;
    const double r21 = matrix[6] / sy;
    const double r02 = matrix[8] / sz;
    const double r12 = matrix[9] / sz;
    const double r22 = matrix[10] / sz;
    double qx = 0.0;
    double qy = 0.0;
    double qz = 0.0;
    double qw = 1.0;
    const double trace = r00 + r11 + r22;
    if (trace > 0.0) {
        const double s = std::sqrt(trace + 1.0) * 2.0;
        qw = 0.25 * s;
        qx = (r21 - r12) / s;
        qy = (r02 - r20) / s;
        qz = (r10 - r01) / s;
    } else if (r00 > r11 && r00 > r22) {
        const double s = std::sqrt(1.0 + r00 - r11 - r22) * 2.0;
        qw = (r21 - r12) / s;
        qx = 0.25 * s;
        qy = (r01 + r10) / s;
        qz = (r02 + r20) / s;
    } else if (r11 > r22) {
        const double s = std::sqrt(1.0 + r11 - r00 - r22) * 2.0;
        qw = (r02 - r20) / s;
        qx = (r01 + r10) / s;
        qy = 0.25 * s;
        qz = (r12 + r21) / s;
    } else {
        const double s = std::sqrt(1.0 + r22 - r00 - r11) * 2.0;
        qw = (r10 - r01) / s;
        qx = (r02 + r20) / s;
        qy = (r12 + r21) / s;
        qz = 0.25 * s;
    }
    const cgltf_float quaternion[4]{
        static_cast<cgltf_float>(qx),
        static_cast<cgltf_float>(qy),
        static_cast<cgltf_float>(qz),
        static_cast<cgltf_float>(qw),
    };
    target.LclRotation.Set(euler_from_quaternion(quaternion));
}

FbxFileTexture* make_texture(
    FbxScene& scene,
    const cgltf_texture& source,
    const std::filesystem::path& staged_file,
    const std::string& name
) {
    FbxFileTexture* texture = FbxFileTexture::Create(&scene, name.c_str());
    texture->SetFileName(staged_file.string().c_str());
    texture->SetRelativeFileName(staged_file.filename().string().c_str());
    texture->SetTextureUse(FbxTexture::eStandard);
    texture->SetMappingType(FbxTexture::eUV);
    texture->SetMaterialUse(FbxFileTexture::eModelMaterial);
    texture->SetSwapUV(false);
    texture->SetAlphaSource(FbxTexture::eNone);
    texture->SetTranslation(0.0, 0.0);
    texture->SetScale(1.0, 1.0);
    texture->SetRotation(0.0, 0.0);
    texture->UVSet.Set("UVSet0");
    if (source.sampler != nullptr) {
        const bool clamp_u = source.sampler->wrap_s == cgltf_wrap_mode_clamp_to_edge;
        const bool clamp_v = source.sampler->wrap_t == cgltf_wrap_mode_clamp_to_edge;
        texture->SetWrapMode(clamp_u ? FbxTexture::eClamp : FbxTexture::eRepeat,
                             clamp_v ? FbxTexture::eClamp : FbxTexture::eRepeat);
    }
    return texture;
}

void connect_texture_view(
    const cgltf_texture_view& view,
    FbxProperty property,
    const std::unordered_map<const cgltf_texture*, FbxFileTexture*>& textures
) {
    if (view.texture == nullptr || !property.IsValid()) {
        return;
    }
    const auto found = textures.find(view.texture);
    if (found != textures.end()) {
        property.ConnectSrcObject(found->second);
    }
}

FbxSurfacePhong* make_material(
    FbxScene& scene,
    const cgltf_material& source,
    const std::unordered_map<const cgltf_texture*, FbxFileTexture*>& textures,
    const std::string& fallback_name
) {
    const std::string name = source.name != nullptr && *source.name != '\0'
        ? source.name
        : fallback_name;
    FbxSurfacePhong* material = FbxSurfacePhong::Create(&scene, name.c_str());
    material->ShadingModel.Set("Phong");
    material->Ambient.Set(FbxDouble3(0.0, 0.0, 0.0));
    material->AmbientFactor.Set(0.0);
    material->DiffuseFactor.Set(1.0);
    material->Specular.Set(FbxDouble3(0.04, 0.04, 0.04));
    material->SpecularFactor.Set(1.0);
    material->Shininess.Set(20.0);
    material->Emissive.Set(FbxDouble3(
        source.emissive_factor[0], source.emissive_factor[1], source.emissive_factor[2]
    ));
    material->EmissiveFactor.Set(1.0);

    if (source.has_pbr_metallic_roughness) {
        const auto& pbr = source.pbr_metallic_roughness;
        material->Diffuse.Set(FbxDouble3(
            pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2]
        ));
        material->TransparencyFactor.Set(1.0 - pbr.base_color_factor[3]);
        connect_texture_view(pbr.base_color_texture, material->Diffuse, textures);

        FbxProperty metallic = FbxProperty::Create(material, FbxDoubleDT, "glTF_MetallicFactor");
        metallic.Set(static_cast<double>(pbr.metallic_factor));
        FbxProperty roughness = FbxProperty::Create(material, FbxDoubleDT, "glTF_RoughnessFactor");
        roughness.Set(static_cast<double>(pbr.roughness_factor));
        FbxProperty packed = FbxProperty::Create(
            material, FbxDouble3DT, "glTF_MetallicRoughness"
        );
        packed.Set(FbxDouble3(1.0, 1.0, 1.0));
        connect_texture_view(pbr.metallic_roughness_texture, packed, textures);
    }
    connect_texture_view(source.normal_texture, material->NormalMap, textures);
    connect_texture_view(source.emissive_texture, material->Emissive, textures);
    FbxProperty double_sided = FbxProperty::Create(material, FbxBoolDT, "glTF_DoubleSided");
    double_sided.Set(source.double_sided != 0);
    return material;
}

FbxMesh* make_mesh(
    FbxScene& scene,
    const cgltf_primitive& primitive,
    const std::string& name,
    GlbToFbxConversionReport& report,
    std::vector<Diagnostic>& diagnostics,
    const std::string& path
) {
    const cgltf_accessor* position_accessor = find_attribute(
        primitive, cgltf_attribute_type_position
    );
    const cgltf_accessor* normal_accessor = find_attribute(
        primitive, cgltf_attribute_type_normal
    );
    const cgltf_accessor* uv_accessor = find_attribute(
        primitive, cgltf_attribute_type_texcoord, 0
    );
    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<float> uvs;
    if (!unpack_floats(
            position_accessor, cgltf_type_vec3, 3, positions, diagnostics, path + ".POSITION"
        )) {
        return nullptr;
    }
    if (normal_accessor != nullptr
        && !unpack_floats(
            normal_accessor, cgltf_type_vec3, 3, normals, diagnostics, path + ".NORMAL"
        )) {
        return nullptr;
    }
    if (uv_accessor != nullptr
        && !unpack_floats(
            uv_accessor, cgltf_type_vec2, 2, uvs, diagnostics, path + ".TEXCOORD_0"
        )) {
        return nullptr;
    }
    if ((!normals.empty() && normal_accessor->count != position_accessor->count)
        || (!uvs.empty() && uv_accessor->count != position_accessor->count)) {
        diagnostics.push_back(conversion_error(
            "GLB_ATTRIBUTE_COUNT",
            "Vertex attribute counts do not match POSITION.",
            path
        ));
        return nullptr;
    }

    std::vector<std::uint32_t> triangles;
    if (!read_triangle_indices(
            primitive, position_accessor->count, triangles, diagnostics, path + ".indices"
        )) {
        return nullptr;
    }

    FbxMesh* mesh = FbxMesh::Create(&scene, name.c_str());
    const int control_point_count = static_cast<int>(position_accessor->count);
    mesh->InitControlPoints(control_point_count);
    FbxVector4* control_points = mesh->GetControlPoints();
    for (int index = 0; index < control_point_count; ++index) {
        const std::size_t offset = static_cast<std::size_t>(index) * 3;
        control_points[index] = FbxVector4(
            positions[offset], positions[offset + 1], positions[offset + 2]
        );
    }

    if (!normals.empty()) {
        FbxGeometryElementNormal* element = mesh->CreateElementNormal();
        element->SetName("Normals");
        element->SetMappingMode(FbxGeometryElement::eByControlPoint);
        element->SetReferenceMode(FbxGeometryElement::eDirect);
        element->GetDirectArray().SetCount(control_point_count);
        for (int index = 0; index < control_point_count; ++index) {
            const std::size_t offset = static_cast<std::size_t>(index) * 3;
            element->GetDirectArray().SetAt(index, FbxVector4(
                normals[offset], normals[offset + 1], normals[offset + 2]
            ));
        }
    }

    if (!uvs.empty()) {
        FbxGeometryElementUV* element = mesh->CreateElementUV("UVSet0");
        element->SetMappingMode(FbxGeometryElement::eByControlPoint);
        element->SetReferenceMode(FbxGeometryElement::eDirect);
        element->GetDirectArray().SetCount(control_point_count);
        for (int index = 0; index < control_point_count; ++index) {
            const std::size_t offset = static_cast<std::size_t>(index) * 2;
            element->GetDirectArray().SetAt(index, FbxVector2(
                uvs[offset], 1.0 - uvs[offset + 1]
            ));
        }
    }

    FbxGeometryElementMaterial* material_element = mesh->CreateElementMaterial();
    material_element->SetMappingMode(FbxGeometryElement::eAllSame);
    material_element->SetReferenceMode(FbxGeometryElement::eIndexToDirect);
    material_element->GetIndexArray().Add(0);

    for (std::size_t index = 0; index < triangles.size(); index += 3) {
        mesh->BeginPolygon(0, -1, -1, false);
        mesh->AddPolygon(static_cast<int>(triangles[index]));
        mesh->AddPolygon(static_cast<int>(triangles[index + 1]));
        mesh->AddPolygon(static_cast<int>(triangles[index + 2]));
        mesh->EndPolygon();
    }

    report.control_point_count += position_accessor->count;
    report.triangle_count += triangles.size() / 3;
    ++report.primitive_count;
    return mesh;
}

#endif

}  // namespace

bool GlbToFbxConversionResult::success() const noexcept {
    return report.has_value() && std::ranges::none_of(
        diagnostics,
        [](const Diagnostic& item) { return item.severity == DiagnosticSeverity::error; }
    );
}

GlbToFbxConversionResult convert_unrigged_glb_to_fbx(
    const std::filesystem::path& source_path,
    const std::filesystem::path& output_path,
    const GlbToFbxConversionOptions& options
) {
#if !defined(UNIFIED3D_HAS_AUTODESK_FBX)
    return {
        .report = std::nullopt,
        .diagnostics = {conversion_error(
            "AUTODESK_FBX_UNAVAILABLE",
            "GLB-to-FBX conversion requires an Autodesk FBX SDK-enabled Runtime build."
        )},
    };
#else
    std::vector<Diagnostic> diagnostics;
    if (source_path.extension() != ".glb" && source_path.extension() != ".GLB") {
        return {
            .report = std::nullopt,
            .diagnostics = {conversion_error(
                "GLB_SOURCE_REQUIRED",
                "The targeted lossless converter requires a binary .glb source."
            )},
        };
    }
    if (output_path.extension() != ".fbx" && output_path.extension() != ".FBX") {
        return {
            .report = std::nullopt,
            .diagnostics = {conversion_error(
                "FBX_OUTPUT_REQUIRED",
                "The conversion output path must use the .fbx extension."
            )},
        };
    }
    std::error_code file_error;
    if (!std::filesystem::is_regular_file(source_path, file_error) || file_error) {
        return {
            .report = std::nullopt,
            .diagnostics = {conversion_error(
                "GLB_SOURCE_MISSING",
                "The source GLB does not exist or is not a regular file."
            )},
        };
    }
    if (std::filesystem::exists(output_path, file_error) && !options.overwrite) {
        return {
            .report = std::nullopt,
            .diagnostics = {conversion_error(
                "FBX_OUTPUT_EXISTS",
                "The output FBX already exists and overwrite is disabled."
            )},
        };
    }
    file_error.clear();
    std::filesystem::create_directories(output_path.parent_path(), file_error);
    if (file_error) {
        return {
            .report = std::nullopt,
            .diagnostics = {conversion_error(
                "FBX_OUTPUT_DIRECTORY",
                "Failed to create the output directory: " + file_error.message()
            )},
        };
    }

    cgltf_options parse_options{};
    cgltf_data* parsed = nullptr;
    const std::string source_utf8 = utf8_path(source_path);
    const cgltf_result parse_result = cgltf_parse_file(
        &parse_options, source_utf8.c_str(), &parsed
    );
    std::unique_ptr<cgltf_data, CgltfDestroy> data{parsed};
    if (parse_result != cgltf_result_success || data == nullptr) {
        return {
            .report = std::nullopt,
            .diagnostics = {conversion_error(
                "GLB_PARSE_FAILED",
                "cgltf failed to parse the source GLB."
            )},
        };
    }
    if (cgltf_load_buffers(&parse_options, data.get(), source_utf8.c_str())
        != cgltf_result_success) {
        return {
            .report = std::nullopt,
            .diagnostics = {conversion_error(
                "GLB_BUFFER_LOAD_FAILED",
                "cgltf failed to load the source GLB buffers."
            )},
        };
    }
    if (cgltf_validate(data.get()) != cgltf_result_success) {
        return {
            .report = std::nullopt,
            .diagnostics = {conversion_error(
                "GLB_VALIDATION_FAILED",
                "cgltf rejected the source GLB before FBX conversion."
            )},
        };
    }
    if (data->skins_count != 0 || data->animations_count != 0) {
        return {
            .report = std::nullopt,
            .diagnostics = {conversion_error(
                "GLB_UNRIGGED_REQUIRED",
                "This conversion profile requires a GLB without skins or animations."
            )},
        };
    }
    for (cgltf_size mesh_index = 0; mesh_index < data->meshes_count; ++mesh_index) {
        const cgltf_mesh& mesh = data->meshes[mesh_index];
        for (cgltf_size primitive_index = 0; primitive_index < mesh.primitives_count; ++primitive_index) {
            const cgltf_primitive& primitive = mesh.primitives[primitive_index];
            if (primitive.targets_count != 0 || primitive.has_draco_mesh_compression) {
                return {
                    .report = std::nullopt,
                    .diagnostics = {conversion_error(
                        "GLB_PROFILE_UNSUPPORTED",
                        "This conversion profile does not silently discard morph targets or Draco data."
                    )},
                };
            }
        }
    }

    GlbToFbxConversionReport report{
        .source_path = std::filesystem::weakly_canonical(source_path),
        .output_path = std::filesystem::absolute(output_path).lexically_normal(),
        .source_size_bytes = std::filesystem::file_size(source_path),
        .mesh_count = data->meshes_count,
        .material_count = data->materials_count,
        .texture_count = data->textures_count,
    };

    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    ExtractedMedia media;
    media.directory = output_path.parent_path()
        / (output_path.stem().string() + ".unified3d-media-" + std::to_string(nonce));
    std::filesystem::create_directory(media.directory, file_error);
    if (file_error) {
        return {
            .report = std::nullopt,
            .diagnostics = {conversion_error(
                "FBX_MEDIA_DIRECTORY",
                "Failed to create the temporary media directory: " + file_error.message()
            )},
        };
    }

    std::unordered_map<const cgltf_image*, std::filesystem::path> image_paths;
    image_paths.reserve(data->images_count);
    for (cgltf_size index = 0; index < data->images_count; ++index) {
        const cgltf_image& image = data->images[index];
        const std::string base_name = sanitize_name(
            image.name != nullptr ? image.name : "",
            "image_" + std::to_string(index)
        );
        const std::filesystem::path staged = media.directory
            / (std::to_string(index) + "_" + base_name + image_extension(image));
        media.files.push_back(staged);
        if (!write_embedded_image(
                image,
                source_path,
                staged,
                diagnostics,
                "$.images[" + std::to_string(index) + "]"
            )) {
            return {.report = std::nullopt, .diagnostics = std::move(diagnostics)};
        }
        image_paths.emplace(&image, staged);
    }

    std::unique_ptr<FbxManager, FbxDestroy<FbxManager>> manager{FbxManager::Create()};
    if (!manager) {
        return {
            .report = std::nullopt,
            .diagnostics = {conversion_error(
                "FBX_SDK_MANAGER",
                "Autodesk FBX SDK manager creation failed."
            )},
        };
    }
    FbxIOSettings* io = FbxIOSettings::Create(manager.get(), IOSROOT);
    manager->SetIOSettings(io);
    io->SetBoolProp(EXP_FBX_MATERIAL, true);
    io->SetBoolProp(EXP_FBX_TEXTURE, true);
    io->SetBoolProp(EXP_FBX_MODEL, true);
    io->SetBoolProp(EXP_FBX_ANIMATION, false);
    io->SetBoolProp(EXP_FBX_SHAPE, false);
    io->SetBoolProp(EXP_FBX_EMBEDDED, options.embed_media);

    std::unique_ptr<FbxScene, FbxDestroy<FbxScene>> scene{
        FbxScene::Create(manager.get(), "Unified3D_GLTF_Conversion")
    };
    if (!scene) {
        return {
            .report = std::nullopt,
            .diagnostics = {conversion_error(
                "FBX_SCENE_CREATE",
                "Autodesk FBX SDK scene creation failed."
            )},
        };
    }
    scene->GetGlobalSettings().SetAxisSystem(FbxAxisSystem::OpenGL);
    scene->GetGlobalSettings().SetSystemUnit(FbxSystemUnit::m);
    if (FbxDocumentInfo* info = FbxDocumentInfo::Create(manager.get(), "Unified3DInfo")) {
        info->mTitle = source_path.filename().string().c_str();
        info->mAuthor = "Unified3D";
        info->mComment = "Lossless-triangle GLB to FBX conversion with embedded media";
        scene->SetSceneInfo(info);
    }

    std::unordered_map<const cgltf_texture*, FbxFileTexture*> textures;
    textures.reserve(data->textures_count);
    for (cgltf_size index = 0; index < data->textures_count; ++index) {
        const cgltf_texture& texture = data->textures[index];
        const cgltf_image* image = texture.image;
        if (image == nullptr && texture.has_webp) {
            image = texture.webp_image;
        }
        if (image == nullptr && texture.has_basisu) {
            image = texture.basisu_image;
        }
        const auto staged = image_paths.find(image);
        if (image == nullptr || staged == image_paths.end()) {
            diagnostics.push_back(conversion_error(
                "GLB_TEXTURE_IMAGE",
                "A glTF texture does not resolve to a staged image.",
                "$.textures[" + std::to_string(index) + "]"
            ));
            return {.report = std::nullopt, .diagnostics = std::move(diagnostics)};
        }
        const std::string name = texture.name != nullptr && *texture.name != '\0'
            ? texture.name
            : "texture_" + std::to_string(index);
        textures.emplace(
            &texture,
            make_texture(*scene, texture, staged->second, name)
        );
    }

    std::vector<FbxSurfacePhong*> materials;
    materials.reserve(data->materials_count);
    for (cgltf_size index = 0; index < data->materials_count; ++index) {
        materials.push_back(make_material(
            *scene,
            data->materials[index],
            textures,
            "material_" + std::to_string(index)
        ));
    }

    std::unordered_map<const cgltf_node*, FbxNode*> nodes;
    nodes.reserve(data->nodes_count);
    for (cgltf_size index = 0; index < data->nodes_count; ++index) {
        const cgltf_node& source_node = data->nodes[index];
        const std::string name = source_node.name != nullptr && *source_node.name != '\0'
            ? source_node.name
            : "node_" + std::to_string(index);
        FbxNode* target_node = FbxNode::Create(scene.get(), name.c_str());
        apply_node_transform(source_node, *target_node);
        target_node->SetShadingMode(FbxNode::eTextureShading);
        nodes.emplace(&source_node, target_node);
    }

    for (cgltf_size index = 0; index < data->nodes_count; ++index) {
        const cgltf_node& source_node = data->nodes[index];
        FbxNode* target_node = nodes.at(&source_node);
        if (source_node.parent != nullptr) {
            nodes.at(source_node.parent)->AddChild(target_node);
        } else {
            scene->GetRootNode()->AddChild(target_node);
        }

        if (source_node.mesh == nullptr) {
            continue;
        }
        const cgltf_mesh& source_mesh = *source_node.mesh;
        for (cgltf_size primitive_index = 0;
             primitive_index < source_mesh.primitives_count;
             ++primitive_index) {
            const cgltf_primitive& primitive = source_mesh.primitives[primitive_index];
            const std::string mesh_name = source_mesh.name != nullptr && *source_mesh.name != '\0'
                ? source_mesh.name
                : "mesh_" + std::to_string(index);
            FbxNode* mesh_node = target_node;
            if (source_mesh.primitives_count > 1) {
                mesh_node = FbxNode::Create(
                    scene.get(),
                    (mesh_name + "_primitive_" + std::to_string(primitive_index)).c_str()
                );
                mesh_node->SetShadingMode(FbxNode::eTextureShading);
                target_node->AddChild(mesh_node);
            }
            FbxMesh* mesh = make_mesh(
                *scene,
                primitive,
                mesh_name + (source_mesh.primitives_count > 1
                    ? "_" + std::to_string(primitive_index)
                    : ""),
                report,
                diagnostics,
                "$.meshes[" + std::to_string(&source_mesh - data->meshes)
                    + "].primitives[" + std::to_string(primitive_index) + "]"
            );
            if (mesh == nullptr) {
                return {.report = std::nullopt, .diagnostics = std::move(diagnostics)};
            }
            mesh_node->SetNodeAttribute(mesh);
            if (primitive.material != nullptr) {
                const std::ptrdiff_t material_index = primitive.material - data->materials;
                if (material_index >= 0
                    && static_cast<cgltf_size>(material_index) < data->materials_count) {
                    mesh_node->AddMaterial(materials[static_cast<std::size_t>(material_index)]);
                }
            }
        }
    }

    std::unique_ptr<FbxExporter, FbxDestroy<FbxExporter>> exporter{
        FbxExporter::Create(manager.get(), "Unified3D_FBX_Exporter")
    };
    const std::string output_native = output_path.string();
    const int writer_format = manager->GetIOPluginRegistry()->GetNativeWriterFormat();
    if (!exporter->Initialize(output_native.c_str(), writer_format, io)) {
        return {
            .report = std::nullopt,
            .diagnostics = {conversion_error(
                "FBX_EXPORT_INITIALIZE",
                exporter->GetStatus().GetErrorString()
            )},
        };
    }
    if (!exporter->Export(scene.get())) {
        return {
            .report = std::nullopt,
            .diagnostics = {conversion_error(
                "FBX_EXPORT_FAILED",
                exporter->GetStatus().GetErrorString()
            )},
        };
    }
    exporter.reset();

    report.output_size_bytes = std::filesystem::file_size(output_path, file_error);
    if (file_error || report.output_size_bytes == 0) {
        return {
            .report = std::nullopt,
            .diagnostics = {conversion_error(
                "FBX_OUTPUT_INVALID",
                "The Autodesk exporter did not produce a non-empty FBX file."
            )},
        };
    }
    report.embedded_media_count = options.embed_media ? data->images_count : 0;
    report.media_embedded = options.embed_media && report.embedded_media_count == data->images_count;
    report.geometry_preserved = report.mesh_count == data->meshes_count
        && report.primitive_count != 0
        && report.triangle_count != 0;
    return {.report = std::move(report), .diagnostics = std::move(diagnostics)};
#endif
}

}  // namespace unified3d::adapters
