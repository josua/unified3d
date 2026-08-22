#include <unified3d/runtime/runtime.hpp>
#include <unified3d/runtime/resource.hpp>
#include <unified3d/runtime/glb_spatial_normalizer.hpp>

#include "json/analysis_codec.hpp"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <istream>
#include <memory>
#include <ostream>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include <unified3d/core/version.hpp>
#include <unified3d/core/geometry/spatial_skin_transfer.hpp>
#include <unified3d/adapters/asset_format_adapter.hpp>
#include <unified3d/adapters/glb_to_fbx_converter.hpp>
#include <unified3d/operations/analysis/compare_analysis_records.hpp>

namespace unified3d::runtime {
namespace {

using Json = nlohmann::json;
using json_codec::DecodeAnalysisResult;

constexpr std::size_t maximum_control_message_bytes = 4U * 1024U * 1024U;

std::filesystem::path path_from_utf8(const std::string& value) {
#if defined(_WIN32)
    std::u8string utf8;
    utf8.reserve(value.size());
    for (const char current : value) {
        utf8.push_back(static_cast<char8_t>(static_cast<unsigned char>(current)));
    }
    return std::filesystem::path{utf8};
#else
    return std::filesystem::path{value};
#endif
}

Json error_response(
    const Json& id,
    const int code,
    const std::string& message,
    Json data = nullptr
) {
    Json error{{"code", code}, {"message", message}};
    if (!data.is_null()) {
        error["data"] = std::move(data);
    }
    return Json{{"jsonrpc", "2.0"}, {"id", id}, {"error", std::move(error)}};
}

Json result_response(const Json& id, Json result) {
    return Json{{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}};
}

bool valid_id(const Json& id) {
    return id.is_null() || id.is_string() || id.is_number_integer()
        || id.is_number_unsigned() || id.is_number_float();
}

Json encode_provenance(const ProvenanceRecord& provenance) {
    Json parents = Json::array();
    for (const ResourceHandle& parent : provenance.parents) {
        parents.push_back(encode_handle(parent));
    }
    return Json{
        {"producer", provenance.producer},
        {"operation_id", provenance.operation_id},
        {
            "source_uri",
            provenance.source_uri.has_value() ? Json(*provenance.source_uri) : Json(nullptr)
        },
        {
            "source_revision",
            provenance.source_revision.has_value()
                ? Json(*provenance.source_revision)
                : Json(nullptr)
        },
        {"parents", std::move(parents)},
    };
}

std::string_view scalar_type_name(const geometry::ScalarType type) {
    switch (type) {
        case geometry::ScalarType::float32: return "FLOAT32";
        case geometry::ScalarType::float64: return "FLOAT64";
        case geometry::ScalarType::uint16: return "UINT16";
        case geometry::ScalarType::uint32: return "UINT32";
    }
    return "UNKNOWN";
}

std::string_view semantic_name(const geometry::VertexSemantic semantic) {
    switch (semantic) {
        case geometry::VertexSemantic::position: return "POSITION";
        case geometry::VertexSemantic::joints: return "JOINTS";
        case geometry::VertexSemantic::weights: return "WEIGHTS";
    }
    return "UNKNOWN";
}

Json encode_vertex_buffer(const VertexBufferDescriptor& buffer) {
    return Json{
        {"id", buffer.handle.encode()},
        {"kind", "VERTEX_BUFFER"},
        {"session", buffer.handle.identity.session},
        {"generation", buffer.handle.identity.generation},
        {"object_id", buffer.handle.identity.object_id},
        {"semantic", semantic_name(buffer.semantic)},
        {"scalar_type", scalar_type_name(buffer.scalar_type)},
        {"component_count", buffer.component_count},
        {"element_count", buffer.element_count},
        {"byte_length", buffer.byte_length},
        {"provenance", encode_provenance(buffer.provenance)},
    };
}

Json encode_index_buffer(const IndexBufferDescriptor& buffer) {
    return Json{
        {"id", buffer.handle.encode()},
        {"kind", "INDEX_BUFFER"},
        {"session", buffer.handle.identity.session},
        {"generation", buffer.handle.identity.generation},
        {"object_id", buffer.handle.identity.object_id},
        {"scalar_type", scalar_type_name(buffer.scalar_type)},
        {"element_count", buffer.element_count},
        {"byte_length", buffer.byte_length},
        {"provenance", encode_provenance(buffer.provenance)},
    };
}

Json encode_skin_buffer(const SkinWeightBufferDescriptor& buffer) {
    return Json{
        {"id", buffer.handle.encode()},
        {"kind", "SKIN_WEIGHT_BUFFER"},
        {"session", buffer.handle.identity.session},
        {"generation", buffer.handle.identity.generation},
        {"object_id", buffer.handle.identity.object_id},
        {"influence_set", buffer.influence_set},
        {"vertex_count", buffer.vertex_count},
        {"component_count", 4},
        {"joint_scalar_type", "UINT32"},
        {"weight_scalar_type", "FLOAT32"},
        {"byte_length", buffer.byte_length},
        {"provenance", encode_provenance(buffer.provenance)},
    };
}

Json encode_asset(const AssetResource& asset) {
    Json primitives = Json::array();
    for (const PrimitiveResourceSet& primitive : asset.primitives) {
        Json influence_sets = Json::array();
        for (const SkinWeightBufferDescriptor& influence : primitive.influence_sets) {
            influence_sets.push_back(encode_skin_buffer(influence));
        }
        Json transform = Json::array();
        for (const double value : primitive.local_to_world) {
            transform.push_back(value);
        }
        primitives.push_back(Json{
            {"name", primitive.name},
            {"source_mesh_index", primitive.source_mesh_index},
            {"source_primitive_index", primitive.source_primitive_index},
            {"domain", adapters::geometry_domain_name(primitive.domain)},
            {"local_to_world", std::move(transform)},
            {"max_influences", primitive.max_influences},
            {"positions", encode_vertex_buffer(primitive.positions)},
            {"indices", primitive.indices.has_value()
                ? encode_index_buffer(*primitive.indices) : Json(nullptr)},
            {"influence_sets", std::move(influence_sets)},
        });
    }
    Json canonical_fingerprint = nullptr;
    if (asset.canonical_geometry_fingerprint.has_value()) {
        const auto& fingerprint = *asset.canonical_geometry_fingerprint;
        canonical_fingerprint = Json{
            {"algorithm", fingerprint.algorithm},
            {"digest", fingerprint.digest},
            {"triangle_count", fingerprint.triangle_count},
            {"position_tolerance_m", fingerprint.position_tolerance_m},
        };
    }
    return Json{
        {"id", asset.handle.encode()},
        {"kind", "3D_ASSET"},
        {"session", asset.handle.identity.session},
        {"generation", asset.handle.identity.generation},
        {"object_id", asset.handle.identity.object_id},
        {"format", asset_format_name(asset.format)},
        {"container", asset_container_name(asset.container)},
        {"path", asset.canonical_path.generic_string()},
        {"size_bytes", asset.size_bytes},
        {"retain_count", asset.retain_count},
        {"adapter", asset.adapter.empty() ? Json(nullptr) : Json(asset.adapter)},
        {"buffer_coordinate_system", asset.buffer_coordinate_system.empty()
            ? Json(nullptr) : Json(asset.buffer_coordinate_system)},
        {"buffer_unit_meters", asset.buffer_unit_meters == 0.0
            ? Json(nullptr) : Json(asset.buffer_unit_meters)},
        {"joint_names", asset.joint_names},
        {"primitives", std::move(primitives)},
        {"canonical_geometry_fingerprint", std::move(canonical_fingerprint)},
        {"provenance", encode_provenance(asset.provenance)},
    };
}

std::optional<adapters::AdapterBackend> decode_backend(const Json& params) {
    if (!params.contains("backend")) {
        return adapters::AdapterBackend::automatic;
    }
    if (!params["backend"].is_string()) {
        return std::nullopt;
    }
    const std::string backend = params["backend"].get<std::string>();
    if (backend == "auto") return adapters::AdapterBackend::automatic;
    if (backend == "cgltf") return adapters::AdapterBackend::cgltf;
    if (backend == "ufbx") return adapters::AdapterBackend::ufbx;
    if (backend == "autodesk_fbx") return adapters::AdapterBackend::autodesk_fbx;
    return std::nullopt;
}

std::optional<AssetHandle> decode_asset_handle(const Json& value) {
    if (!value.is_object() || !value.contains("id") || !value["id"].is_string()
        || !value.contains("kind") || value["kind"] != "3D_ASSET"
        || !value.contains("session") || !value["session"].is_string()
        || !value.contains("generation") || !value["generation"].is_number_unsigned()
        || !value.contains("object_id") || !value["object_id"].is_number_unsigned()) {
        return std::nullopt;
    }
    AssetHandle handle{
        {
            value["session"].get<std::string>(),
            value["generation"].get<std::uint64_t>(),
            value["object_id"].get<std::uint64_t>(),
        },
    };
    if (value["id"].get<std::string>() != handle.encode()) {
        return std::nullopt;
    }
    return handle;
}

void prefix_diagnostics(
    std::vector<Diagnostic>& destination,
    const DecodeAnalysisResult& source,
    const std::string& input
) {
    for (const Diagnostic& diagnostic : source.diagnostics) {
        Diagnostic copy = diagnostic;
        const std::string suffix = diagnostic.path.starts_with("$.")
            ? diagnostic.path.substr(2)
            : diagnostic.path == "$" ? std::string{} : diagnostic.path;
        copy.path = "$.params." + input;
        if (!suffix.empty()) {
            copy.path += "." + suffix;
        }
        destination.push_back(std::move(copy));
    }
}

}  // namespace

struct Runtime::Impl {
    bool shutdown{false};
    AssetRegistry assets;

    std::optional<Json> dispatch(const Json& request) {
        const bool notification = request.is_object() && !request.contains("id");
        const Json id = request.is_object() && request.contains("id")
            ? request["id"]
            : Json{nullptr};

        const auto fail_request = [&](const int code, const std::string& message, Json data = nullptr)
            -> std::optional<Json> {
            if (notification) {
                return std::nullopt;
            }
            return error_response(id, code, message, std::move(data));
        };

        if (!request.is_object()) {
            return error_response(nullptr, -32600, "Invalid Request");
        }
        if (!request.contains("jsonrpc") || request["jsonrpc"] != "2.0"
            || !request.contains("method") || !request["method"].is_string()) {
            return fail_request(-32600, "Invalid Request");
        }
        if (request.contains("id") && !valid_id(request["id"])) {
            return error_response(nullptr, -32600, "Invalid Request");
        }
        if (request.contains("params")
            && !request["params"].is_object()
            && !request["params"].is_array()) {
            return fail_request(-32602, "Invalid params");
        }

        const std::string method = request["method"].get<std::string>();
        const Json params = request.contains("params") ? request["params"] : Json::object();

        if (method == "runtime.hello") {
            Json capabilities = Json::array(
                {
                    "transport.stdio",
                    "analysis.validate",
                    "analysis.compare",
                    "asset.load",
                    "asset.normalize_spatial",
                    "asset.release",
                    "skin.transfer",
                    "resource.geometry_buffers",
                    "resource.canonical_geometry_fingerprint",
                    "resource.provenance",
                }
            );
            if (named_pipe_supported()) {
                capabilities.push_back("transport.windows_named_pipe");
            }
            if (adapters::autodesk_fbx_available()) {
                capabilities.push_back("asset.convert_glb_to_fbx");
            }
            Json result{
                {"runtime_version", "0.2.0-dev"},
                {"core_version", unified3d::version},
                {"protocol_version", "1.0"},
                {"analysis_schema", unified3d::analysis_schema},
                {"analysis_comparison_schema", unified3d::analysis_comparison_schema},
                {"session_id", assets.session_id()},
                {"live_asset_count", assets.live_asset_count()},
                {"native_adapters", Json{
                    {"cgltf", true},
                    {"ufbx", true},
                    {"autodesk_fbx", adapters::autodesk_fbx_available()},
                }},
                {"capabilities", std::move(capabilities)},
            };
            return notification ? std::nullopt : std::optional<Json>{result_response(id, std::move(result))};
        }

        if (method == "runtime.shutdown") {
            shutdown = true;
            Json result{{"shutdown", true}};
            return notification ? std::nullopt : std::optional<Json>{result_response(id, std::move(result))};
        }

        if (method == "analysis.validate") {
            if (!params.is_object() || !params.contains("analysis")
                || !params["analysis"].is_object()) {
                return fail_request(
                    -32602,
                    "Invalid params",
                    Json{{"required", Json::array({"analysis"})}}
                );
            }
            const DecodeAnalysisResult decoded =
                json_codec::decode_analysis_record(params["analysis"]);
            Json result{
                {"schema", unified3d::analysis_schema},
                {"valid", decoded.valid()},
                {"diagnostics", json_codec::encode_diagnostics(decoded.diagnostics)},
            };
            return notification ? std::nullopt : std::optional<Json>{result_response(id, std::move(result))};
        }

        if (method == "analysis.compare") {
            if (!params.is_object() || !params.contains("a") || !params["a"].is_object()
                || !params.contains("b") || !params["b"].is_object()) {
                return fail_request(
                    -32602,
                    "Invalid params",
                    Json{{"required", Json::array({"a", "b"})}}
                );
            }
            const DecodeAnalysisResult decoded_a =
                json_codec::decode_analysis_record(params["a"]);
            const DecodeAnalysisResult decoded_b =
                json_codec::decode_analysis_record(params["b"]);
            if (!decoded_a.valid() || !decoded_b.valid()) {
                std::vector<Diagnostic> diagnostics;
                diagnostics.reserve(
                    decoded_a.diagnostics.size() + decoded_b.diagnostics.size()
                );
                prefix_diagnostics(diagnostics, decoded_a, "a");
                prefix_diagnostics(diagnostics, decoded_b, "b");
                return fail_request(
                    -32010,
                    "Analysis validation failed",
                    Json{{"diagnostics", json_codec::encode_diagnostics(diagnostics)}}
                );
            }

            const auto operation =
                unified3d::operations::analysis::compare_analysis_records(
                    *decoded_a.record,
                    *decoded_b.record
                );
            if (!operation.success()) {
                return fail_request(
                    -32011,
                    "Analysis comparison failed",
                    Json{{"diagnostics", json_codec::encode_diagnostics(operation.diagnostics)}}
                );
            }
            Json result = json_codec::encode_analysis_comparison(
                *operation.comparison,
                params["a"],
                params["b"]
            );
            return notification ? std::nullopt : std::optional<Json>{result_response(id, std::move(result))};
        }

        if (method == "asset.load") {
            if (!params.is_object() || !params.contains("path") || !params["path"].is_string()
                || params["path"].get_ref<const std::string&>().empty()) {
                return fail_request(
                    -32602,
                    "Invalid params",
                    Json{{"required", Json::array({"path"})}}
                );
            }
            const auto backend = decode_backend(params);
            if (!backend.has_value()) {
                return fail_request(
                    -32602,
                    "Invalid params",
                    Json{{"field", "backend"}, {"accepted", Json::array({"auto", "cgltf", "ufbx", "autodesk_fbx"})}}
                );
            }
            const std::filesystem::path asset_path = path_from_utf8(
                params["path"].get<std::string>()
            );
            const adapters::AdapterBackend selected_backend =
                adapters::resolve_adapter_backend(asset_path, *backend);
            auto loaded = assets.load(
                asset_path,
                adapters::adapter_backend_name(selected_backend)
            );
            if (!loaded.success()) {
                return fail_request(
                    -32021,
                    "Asset load failed",
                    Json{
                        {"code", loaded.error->code},
                        {"detail", loaded.error->message},
                    }
                );
            }
            if (!loaded.reused) {
                adapters::DecodeResult decoded = adapters::decode_asset_buffers(
                    loaded.asset->canonical_path,
                    selected_backend
                );
                if (!decoded.success()) {
                    static_cast<void>(assets.release(loaded.asset->handle));
                    return fail_request(
                        -32022,
                        "Native asset decoding failed",
                        Json{
                            {"backend", adapters::adapter_backend_name(selected_backend)},
                            {"diagnostics", json_codec::encode_diagnostics(decoded.diagnostics)},
                        }
                    );
                }
                RegisterBuffersResult registered = assets.register_buffers(
                    loaded.asset->handle,
                    std::move(*decoded.asset)
                );
                if (!registered.success()) {
                    static_cast<void>(assets.release(loaded.asset->handle));
                    return fail_request(
                        -32023,
                        "Runtime buffer registration failed",
                        Json{
                            {"code", registered.error->code},
                            {"detail", registered.error->message},
                        }
                    );
                }
                loaded.asset = std::move(registered.asset);
            }
            Json result{
                {"asset", encode_asset(*loaded.asset)},
                {"reused", loaded.reused},
            };
            return notification ? std::nullopt : std::optional<Json>{result_response(id, std::move(result))};
        }

        if (method == "asset.release") {
            if (!params.is_object() || !params.contains("asset")) {
                return fail_request(
                    -32602,
                    "Invalid params",
                    Json{{"required", Json::array({"asset"})}}
                );
            }
            const auto handle = decode_asset_handle(params["asset"]);
            if (!handle.has_value()) {
                return fail_request(-32602, "Invalid params", Json{{"field", "asset"}});
            }
            const auto released = assets.release(*handle);
            if (!released.success()) {
                return fail_request(
                    -32020,
                    "Invalid asset handle",
                    Json{
                        {"code", released.error->code},
                        {"detail", released.error->message},
                    }
                );
            }
            Json result{
                {"released", released.released},
                {"remaining_references", released.remaining_references},
            };
            return notification ? std::nullopt : std::optional<Json>{result_response(id, std::move(result))};
        }

        if (method == "asset.convert_glb_to_fbx") {
            if (!params.is_object() || !params.contains("asset")
                || !params.contains("output_path") || !params["output_path"].is_string()
                || params["output_path"].get_ref<const std::string&>().empty()) {
                return fail_request(
                    -32602,
                    "Invalid params",
                    Json{{"required", Json::array({"asset", "output_path"})}}
                );
            }
            const auto handle = decode_asset_handle(params["asset"]);
            if (!handle.has_value()) {
                return fail_request(-32602, "Invalid params", Json{{"field", "asset"}});
            }
            const auto source_asset = assets.find(*handle);
            if (!source_asset.has_value()) {
                return fail_request(
                    -32020,
                    "Invalid asset handle",
                    Json{{"code", "STALE_HANDLE"}}
                );
            }
            if (source_asset->container != analysis::AssetContainer::glb) {
                return fail_request(
                    -32040,
                    "GLB-to-FBX conversion requires a GLB asset",
                    Json{{"container", asset_container_name(source_asset->container)}}
                );
            }

            adapters::GlbToFbxConversionOptions options;
            if (params.contains("embed_media")) {
                if (!params["embed_media"].is_boolean()) {
                    return fail_request(-32602, "Invalid params", Json{{"field", "embed_media"}});
                }
                options.embed_media = params["embed_media"].get<bool>();
            }
            if (params.contains("overwrite")) {
                if (!params["overwrite"].is_boolean()) {
                    return fail_request(-32602, "Invalid params", Json{{"field", "overwrite"}});
                }
                options.overwrite = params["overwrite"].get<bool>();
            }
            const std::filesystem::path output_path = path_from_utf8(
                params["output_path"].get<std::string>()
            );
            adapters::GlbToFbxConversionResult converted =
                adapters::convert_unrigged_glb_to_fbx(
                    source_asset->canonical_path, output_path, options
                );
            if (!converted.success()) {
                return fail_request(
                    -32041,
                    "GLB-to-FBX conversion failed",
                    Json{{"diagnostics", json_codec::encode_diagnostics(converted.diagnostics)}}
                );
            }

            auto loaded = assets.load(converted.report->output_path, "autodesk_fbx");
            if (!loaded.success()) {
                return fail_request(
                    -32042,
                    "Converted FBX registration failed",
                    Json{{"code", loaded.error->code}, {"detail", loaded.error->message}}
                );
            }
            if (!loaded.reused) {
                adapters::DecodeResult decoded = adapters::decode_asset_buffers(
                    loaded.asset->canonical_path,
                    adapters::AdapterBackend::autodesk_fbx
                );
                if (!decoded.success()) {
                    static_cast<void>(assets.release(loaded.asset->handle));
                    return fail_request(
                        -32043,
                        "Converted FBX validation failed",
                        Json{{"diagnostics", json_codec::encode_diagnostics(decoded.diagnostics)}}
                    );
                }
                RegisterBuffersResult registered = assets.register_buffers(
                    loaded.asset->handle, std::move(*decoded.asset)
                );
                if (!registered.success()) {
                    static_cast<void>(assets.release(loaded.asset->handle));
                    return fail_request(
                        -32044,
                        "Converted FBX buffer registration failed",
                        Json{{"code", registered.error->code}, {"detail", registered.error->message}}
                    );
                }
                loaded.asset = std::move(registered.asset);
            }
            bool geometry_preserved = false;
            if (source_asset->canonical_geometry_fingerprint.has_value()
                && loaded.asset->canonical_geometry_fingerprint.has_value()) {
                const auto& source_fingerprint =
                    *source_asset->canonical_geometry_fingerprint;
                const auto& converted_fingerprint =
                    *loaded.asset->canonical_geometry_fingerprint;
                geometry_preserved =
                    source_fingerprint.algorithm == converted_fingerprint.algorithm
                    && source_fingerprint.digest == converted_fingerprint.digest
                    && source_fingerprint.triangle_count
                        == converted_fingerprint.triangle_count
                    && source_fingerprint.position_tolerance_m
                        == converted_fingerprint.position_tolerance_m;
            }
            if (!geometry_preserved) {
                static_cast<void>(assets.release(loaded.asset->handle));
                return fail_request(
                    -32046,
                    "Converted FBX geometry validation failed",
                    Json{{"code", "CANONICAL_GEOMETRY_MISMATCH"}}
                );
            }
            converted.report->geometry_preserved = true;
            const auto provenance = assets.update_provenance(
                loaded.asset->handle,
                ProvenanceRecord{
                    .producer = "asset.convert_glb_to_fbx",
                    .operation_id = "convert-glb-to-fbx:" + assets.session_id() + ":"
                        + std::to_string(loaded.asset->handle.identity.generation) + ":"
                        + std::to_string(loaded.asset->handle.identity.object_id),
                    .source_uri = converted.report->output_path.generic_string(),
                    .source_revision = std::nullopt,
                    .parents = {source_asset->handle},
                }
            );
            if (!provenance.success()) {
                static_cast<void>(assets.release(loaded.asset->handle));
                return fail_request(
                    -32045,
                    "Converted FBX provenance registration failed",
                    Json{{"code", provenance.error->code}, {"detail", provenance.error->message}}
                );
            }
            loaded.asset = provenance.asset;
            const adapters::GlbToFbxConversionReport& report = *converted.report;
            Json result{
                {"schema", "unified3d.glb-to-fbx-conversion/1.0-draft"},
                {"source_asset", encode_asset(*source_asset)},
                {"converted_asset", encode_asset(*loaded.asset)},
                {"report", Json{
                    {"source_path", report.source_path.generic_string()},
                    {"output_path", report.output_path.generic_string()},
                    {"source_size_bytes", report.source_size_bytes},
                    {"output_size_bytes", report.output_size_bytes},
                    {"mesh_count", report.mesh_count},
                    {"primitive_count", report.primitive_count},
                    {"control_point_count", report.control_point_count},
                    {"triangle_count", report.triangle_count},
                    {"material_count", report.material_count},
                    {"texture_count", report.texture_count},
                    {"embedded_media_count", report.embedded_media_count},
                    {"geometry_preserved", report.geometry_preserved},
                    {"media_embedded", report.media_embedded},
                }},
            };
            return notification ? std::nullopt : std::optional<Json>{result_response(id, std::move(result))};
        }

        if (method == "asset.normalize_spatial") {
            if (!params.is_object() || !params.contains("asset")
                || !params.contains("output_path") || !params["output_path"].is_string()
                || params["output_path"].get_ref<const std::string&>().empty()) {
                return fail_request(
                    -32602,
                    "Invalid params",
                    Json{{"required", Json::array({"asset", "output_path"})}}
                );
            }
            const auto handle = decode_asset_handle(params["asset"]);
            if (!handle.has_value()) {
                return fail_request(-32602, "Invalid params", Json{{"field", "asset"}});
            }
            const auto source_asset = assets.find(*handle);
            if (!source_asset.has_value()) {
                return fail_request(
                    -32020,
                    "Invalid asset handle",
                    Json{{"code", "STALE_HANDLE"}}
                );
            }
            if (source_asset->container != analysis::AssetContainer::glb) {
                return fail_request(
                    -32030,
                    "Spatial normalization requires a GLB asset",
                    Json{{"container", asset_container_name(source_asset->container)}}
                );
            }

            GlbSpatialNormalizationOptions options;
            if (params.contains("expected_position_height_m")) {
                if (params["expected_position_height_m"].is_null()) {
                    options.expected_position_height_m = std::nullopt;
                } else if (params["expected_position_height_m"].is_number()) {
                    options.expected_position_height_m =
                        params["expected_position_height_m"].get<double>();
                } else {
                    return fail_request(-32602, "Invalid params", Json{{"field", "expected_position_height_m"}});
                }
            }
            if (params.contains("height_tolerance_m")) {
                if (!params["height_tolerance_m"].is_number()) {
                    return fail_request(-32602, "Invalid params", Json{{"field", "height_tolerance_m"}});
                }
                options.height_tolerance_m = params["height_tolerance_m"].get<double>();
            }
            if (params.contains("correct_scale_factor")) {
                if (!params["correct_scale_factor"].is_boolean()) {
                    return fail_request(-32602, "Invalid params", Json{{"field", "correct_scale_factor"}});
                }
                options.correct_scale_factor = params["correct_scale_factor"].get<bool>();
            }
            if (params.contains("remove_emissive_channel")) {
                if (!params["remove_emissive_channel"].is_boolean()) {
                    return fail_request(-32602, "Invalid params", Json{{"field", "remove_emissive_channel"}});
                }
                options.remove_emissive_channel = params["remove_emissive_channel"].get<bool>();
            }
            if (params.contains("remove_head_helper_bones")) {
                if (!params["remove_head_helper_bones"].is_boolean()) {
                    return fail_request(-32602, "Invalid params", Json{{"field", "remove_head_helper_bones"}});
                }
                options.remove_head_helper_bones = params["remove_head_helper_bones"].get<bool>();
            }
            if (params.contains("remove_animations")) {
                if (!params["remove_animations"].is_boolean()) {
                    return fail_request(-32602, "Invalid params", Json{{"field", "remove_animations"}});
                }
                options.remove_animations = params["remove_animations"].get<bool>();
            }
            if (params.contains("overwrite")) {
                if (!params["overwrite"].is_boolean()) {
                    return fail_request(-32602, "Invalid params", Json{{"field", "overwrite"}});
                }
                options.overwrite = params["overwrite"].get<bool>();
            }
            const std::filesystem::path output_path = path_from_utf8(
                params["output_path"].get<std::string>()
            );
            const GlbSpatialNormalizationResult normalized = normalize_mixed_unit_rig_glb(
                source_asset->canonical_path, output_path, options
            );
            if (!normalized.success()) {
                return fail_request(
                    -32031,
                    "GLB spatial normalization failed",
                    Json{{"diagnostics", json_codec::encode_diagnostics(normalized.diagnostics)}}
                );
            }

            auto loaded = assets.load(normalized.report->output_path, "cgltf");
            if (!loaded.success()) {
                return fail_request(
                    -32032,
                    "Normalized GLB registration failed",
                    Json{{"code", loaded.error->code}, {"detail", loaded.error->message}}
                );
            }
            if (!loaded.reused) {
                adapters::DecodeResult decoded = adapters::decode_asset_buffers(
                    loaded.asset->canonical_path, adapters::AdapterBackend::cgltf
                );
                if (!decoded.success()) {
                    static_cast<void>(assets.release(loaded.asset->handle));
                    return fail_request(
                        -32033,
                        "Normalized GLB validation failed",
                        Json{{"diagnostics", json_codec::encode_diagnostics(decoded.diagnostics)}}
                    );
                }
                RegisterBuffersResult registered = assets.register_buffers(
                    loaded.asset->handle, std::move(*decoded.asset)
                );
                if (!registered.success()) {
                    static_cast<void>(assets.release(loaded.asset->handle));
                    return fail_request(
                        -32034,
                        "Normalized GLB buffer registration failed",
                        Json{{"code", registered.error->code}, {"detail", registered.error->message}}
                    );
                }
                loaded.asset = std::move(registered.asset);
            }
            const auto provenance = assets.update_provenance(
                loaded.asset->handle,
                ProvenanceRecord{
                    .producer = "asset.normalize_spatial",
                    .operation_id = "normalize-spatial:" + assets.session_id() + ":"
                        + std::to_string(loaded.asset->handle.identity.generation) + ":"
                        + std::to_string(loaded.asset->handle.identity.object_id),
                    .source_uri = normalized.report->output_path.generic_string(),
                    .source_revision = std::nullopt,
                    .parents = {source_asset->handle},
                }
            );
            if (!provenance.success()) {
                static_cast<void>(assets.release(loaded.asset->handle));
                return fail_request(
                    -32035,
                    "Normalized GLB provenance registration failed",
                    Json{{"code", provenance.error->code}, {"detail", provenance.error->message}}
                );
            }
            loaded.asset = provenance.asset;
            const GlbSpatialNormalizationReport& report = *normalized.report;
            Json result{
                {"schema", "unified3d.spatial-normalization/1.0-draft"},
                {"source_asset", encode_asset(*source_asset)},
                {"normalized_asset", encode_asset(*loaded.asset)},
                {"report", Json{
                    {"source_path", report.source_path.generic_string()},
                    {"output_path", report.output_path.generic_string()},
                    {"source_size_bytes", report.source_size_bytes},
                    {"output_size_bytes", report.output_size_bytes},
                    {"root_node_index", report.root_node_index},
                    {"root_node_name", report.root_node_name},
                    {"absorbed_uniform_scale", report.absorbed_uniform_scale},
                    {"position_height_m", report.position_height_m},
                    {"modified_node_translation_count", report.modified_node_translation_count},
                    {"modified_animation_accessor_count", report.modified_animation_accessor_count},
                    {"modified_inverse_bind_matrix_count", report.modified_inverse_bind_matrix_count},
                    {"removed_emissive_texture_count", report.removed_emissive_texture_count},
                    {"zeroed_emissive_factor_count", report.zeroed_emissive_factor_count},
                    {"removed_head_helper_node_count", report.removed_head_helper_node_count},
                    {"removed_head_helper_joint_count", report.removed_head_helper_joint_count},
                    {"removed_head_helper_animation_channel_count", report.removed_head_helper_animation_channel_count},
                    {"removed_animation_clip_count", report.removed_animation_clip_count},
                    {"removed_animation_channel_count", report.removed_animation_channel_count},
                    {"removed_animation_sampler_count", report.removed_animation_sampler_count},
                    {"scale_correction_applied", report.scale_correction_applied},
                    {"emissive_correction_applied", report.emissive_correction_applied},
                    {"head_helper_bone_removal_applied", report.head_helper_bone_removal_applied},
                    {"animation_removal_applied", report.animation_removal_applied},
                }},
            };
            return notification ? std::nullopt : std::optional<Json>{result_response(id, std::move(result))};
        }

        if (method == "skin.transfer") {
            if (!params.is_object() || !params.contains("source") || !params.contains("target")) {
                return fail_request(
                    -32602,
                    "Invalid params",
                    Json{{"required", Json::array({"source", "target"})}}
                );
            }
            const auto source_handle = decode_asset_handle(params["source"]);
            const auto target_handle = decode_asset_handle(params["target"]);
            if (!source_handle || !target_handle || *source_handle == *target_handle) {
                return fail_request(
                    -32602,
                    "Invalid params",
                    Json{{"detail", "source and target must be distinct live 3D asset handles"}}
                );
            }
            geometry::SpatialSkinTransferOptions options;
            options.maximum_distance_m = 0.05;
            bool replace_existing = false;
            if (params.contains("quality")) {
                if (!params["quality"].is_string()) {
                    return fail_request(-32602, "Invalid params", Json{{"field", "quality"}});
                }
                const std::string quality = params["quality"].get<std::string>();
                if (quality == "fast") options.quality = geometry::SpatialTransferQuality::fast;
                else if (quality == "balanced") options.quality = geometry::SpatialTransferQuality::balanced;
                else if (quality == "precise") options.quality = geometry::SpatialTransferQuality::precise;
                else if (quality == "diagnostic") options.quality = geometry::SpatialTransferQuality::diagnostic;
                else return fail_request(-32602, "Invalid params", Json{{"field", "quality"}});
            }
            if (params.contains("maximum_influences")) {
                if (!params["maximum_influences"].is_number_unsigned()) {
                    return fail_request(-32602, "Invalid params", Json{{"field", "maximum_influences"}});
                }
                options.maximum_influences = params["maximum_influences"].get<std::uint32_t>();
            }
            if (params.contains("minimum_weight")) {
                if (!params["minimum_weight"].is_number()) {
                    return fail_request(-32602, "Invalid params", Json{{"field", "minimum_weight"}});
                }
                options.minimum_weight = params["minimum_weight"].get<double>();
            }
            if (params.contains("maximum_distance_m")) {
                if (params["maximum_distance_m"].is_null()) {
                    options.maximum_distance_m = std::nullopt;
                } else if (params["maximum_distance_m"].is_number()) {
                    options.maximum_distance_m = params["maximum_distance_m"].get<double>();
                } else {
                    return fail_request(-32602, "Invalid params", Json{{"field", "maximum_distance_m"}});
                }
            }
            if (params.contains("replace_existing")) {
                if (!params["replace_existing"].is_boolean()) {
                    return fail_request(-32602, "Invalid params", Json{{"field", "replace_existing"}});
                }
                replace_existing = params["replace_existing"].get<bool>();
            }
            const auto source_asset = assets.find(*source_handle);
            const auto target_asset = assets.find(*target_handle);
            if (!source_asset || !target_asset) {
                return fail_request(-32020, "Invalid asset handle", Json{{"code", "STALE_HANDLE"}});
            }
            if (source_asset->joint_names.empty()) {
                return fail_request(
                    -32040,
                    "Skin donor has no joint table",
                    Json{{"source", source_asset->handle.encode()}}
                );
            }
            const ResolveBuffersResult source_buffers = assets.resolve_buffers(*source_handle);
            const ResolveBuffersResult target_buffers = assets.resolve_buffers(*target_handle);
            if (!source_buffers.success() || !target_buffers.success()) {
                return fail_request(
                    -32041,
                    "Runtime geometry buffers could not be resolved",
                    Json{{"source_error", source_buffers.error ? Json(source_buffers.error->code) : Json(nullptr)},
                         {"target_error", target_buffers.error ? Json(target_buffers.error->code) : Json(nullptr)}}
                );
            }
            std::vector<geometry::SpatialPrimitiveInput> source_inputs;
            std::vector<geometry::SpatialPrimitiveInput> target_inputs;
            source_inputs.reserve(source_buffers.buffers->primitives.size());
            target_inputs.reserve(target_buffers.buffers->primitives.size());
            for (const adapters::DecodedPrimitive& primitive : source_buffers.buffers->primitives) {
                source_inputs.push_back({&primitive.buffers, primitive.local_to_world});
            }
            for (const adapters::DecodedPrimitive& primitive : target_buffers.buffers->primitives) {
                target_inputs.push_back({&primitive.buffers, primitive.local_to_world});
            }
            geometry::SpatialSkinTransferResult transferred =
                geometry::transfer_skin_weights_spatially(source_inputs, target_inputs, options);
            if (!transferred.success()) {
                return fail_request(
                    -32042,
                    "Spatial skin transfer failed",
                    Json{{"diagnostics", json_codec::encode_diagnostics(transferred.diagnostics)}}
                );
            }
            RegisterBuffersResult registered = assets.register_transferred_skin(
                *target_handle,
                *source_handle,
                source_asset->joint_names,
                std::move(transferred.primitives),
                replace_existing
            );
            if (!registered.success()) {
                return fail_request(
                    -32043,
                    "Transferred skin resource registration failed",
                    Json{{"code", registered.error->code}, {"detail", registered.error->message}}
                );
            }
            const geometry::SpatialSkinTransferReport& report = *transferred.report;
            Json samples = Json::array();
            for (const geometry::SpatialMappingSample& sample : report.diagnostic_samples) {
                samples.push_back(Json{
                    {"target_vertex", sample.target_vertex},
                    {"source_triangle", sample.source_triangle},
                    {"barycentric", sample.barycentric},
                    {"distance_m", sample.distance_m},
                });
            }
            Json result{
                {"schema", "unified3d.skin-transfer/1.0-draft"},
                {"method", "spatial_surface"},
                {"source_asset", encode_asset(*source_asset)},
                {"target_asset", encode_asset(*registered.asset)},
                {"report", Json{
                    {"source_triangle_count", report.source_triangle_count},
                    {"target_vertex_count", report.target_vertex_count},
                    {"matched_vertex_count", report.matched_vertex_count},
                    {"rejected_vertex_count", report.rejected_vertex_count},
                    {"mean_distance_m", report.mean_distance_m},
                    {"maximum_distance_m", report.maximum_distance_m},
                    {"output_max_influences", report.output_max_influences},
                    {"diagnostic_samples", std::move(samples)},
                }},
            };
            return notification ? std::nullopt : std::optional<Json>{result_response(id, std::move(result))};
        }

        return fail_request(-32601, "Method not found");
    }
};

Runtime::Runtime() : impl_(std::make_unique<Impl>()) {}
Runtime::~Runtime() = default;
Runtime::Runtime(Runtime&&) noexcept = default;
Runtime& Runtime::operator=(Runtime&&) noexcept = default;

std::optional<std::string> Runtime::handle_message(const std::string_view message) {
    if (message.size() > maximum_control_message_bytes) {
        return error_response(
            nullptr,
            -32001,
            "Control message exceeds the 4 MiB limit"
        ).dump();
    }

    Json request;
    try {
        request = Json::parse(message);
    } catch (const Json::parse_error&) {
        return error_response(nullptr, -32700, "Parse error").dump();
    }

    try {
        const std::optional<Json> response = impl_->dispatch(request);
        return response.has_value()
            ? std::optional<std::string>{response->dump()}
            : std::nullopt;
    } catch (const std::exception& error) {
        const Json id = request.is_object() && request.contains("id")
            && valid_id(request["id"])
            ? request["id"]
            : Json{nullptr};
        return error_response(
            id,
            -32603,
            "Internal error",
            Json{{"detail", error.what()}}
        ).dump();
    }
}

bool Runtime::shutdown_requested() const noexcept {
    return impl_->shutdown;
}

int run_stdio(Runtime& runtime, std::istream& input, std::ostream& output) {
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }
        const std::optional<std::string> response = runtime.handle_message(line);
        if (response.has_value()) {
            output << *response << '\n';
            output.flush();
        }
        if (runtime.shutdown_requested()) {
            break;
        }
    }
    return 0;
}

}  // namespace unified3d::runtime
