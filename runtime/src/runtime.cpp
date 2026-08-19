#include <unified3d/runtime/runtime.hpp>
#include <unified3d/runtime/resource.hpp>

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
#include <unified3d/adapters/asset_format_adapter.hpp>
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
                    "asset.release",
                    "resource.geometry_buffers",
                    "resource.provenance",
                }
            );
            if (named_pipe_supported()) {
                capabilities.push_back("transport.windows_named_pipe");
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
