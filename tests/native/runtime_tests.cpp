#include <unified3d/runtime/runtime.hpp>
#include <unified3d/runtime/resource.hpp>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

#include <nlohmann/json.hpp>

namespace {

using Json = nlohmann::json;

int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (condition) {
        return;
    }
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

Json read_json_fixture(const std::string_view name) {
    const std::filesystem::path path =
        std::filesystem::path{UNIFIED3D_SOURCE_DIR}
        / "sdk" / "python" / "tests" / "fixtures" / name;
    std::ifstream input{path};
    if (!input) {
        throw std::runtime_error("Cannot open fixture: " + path.string());
    }
    return Json::parse(input);
}

Json call(
    unified3d::runtime::Runtime& runtime,
    const std::string_view method,
    Json params = Json::object(),
    Json id = 1
) {
    const Json request{
        {"jsonrpc", "2.0"},
        {"id", std::move(id)},
        {"method", method},
        {"params", std::move(params)},
    };
    const std::optional<std::string> response = runtime.handle_message(request.dump());
    if (!response.has_value()) {
        throw std::runtime_error("Expected a JSON-RPC response for " + std::string{method});
    }
    return Json::parse(*response);
}

std::filesystem::path temporary_asset(const std::string_view extension) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path path = std::filesystem::temp_directory_path()
        / ("unified3d-runtime-test-" + std::to_string(stamp) + std::string{extension});
    std::ofstream output{path, std::ios::binary};
    output << "Unified3D test asset";
    if (!output) {
        throw std::runtime_error("Cannot create temporary asset: " + path.string());
    }
    return path;
}

std::filesystem::path temporary_gltf_asset() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path path = std::filesystem::temp_directory_path()
        / ("unified3d-runtime-test-" + std::to_string(stamp) + ".gltf");
    std::ofstream output{path, std::ios::binary};
    output << R"({
      "asset":{"version":"2.0","generator":"Unified3D test"},
      "buffers":[{"uri":"data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAABAAIA","byteLength":42}],
      "bufferViews":[
        {"buffer":0,"byteOffset":0,"byteLength":36,"target":34962},
        {"buffer":0,"byteOffset":36,"byteLength":6,"target":34963}
      ],
      "accessors":[
        {"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},
        {"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}
      ],
      "meshes":[{"name":"triangle","primitives":[{"attributes":{"POSITION":0},"indices":1,"mode":4}]}],
      "nodes":[{"name":"triangle-node","mesh":0}],
      "scenes":[{"nodes":[0]}],"scene":0
    })";
    if (!output) {
        throw std::runtime_error("Cannot create temporary glTF asset: " + path.string());
    }
    return path;
}

void protocol_tests() {
    unified3d::runtime::Runtime runtime;

    const Json hello = call(runtime, "runtime.hello", Json::object(), "hello");
    expect(hello["jsonrpc"] == "2.0", "hello must use JSON-RPC 2.0");
    expect(hello["id"] == "hello", "hello must preserve a string id");
    expect(
        hello["result"]["analysis_schema"] == "unified3d.analysis/1.0-rc1",
        "hello must advertise the RC1 analysis schema"
    );
    expect(
        hello["result"]["capabilities"].size() >= 6U,
        "hello must advertise all initial runtime capabilities"
    );
    expect(
        std::ranges::find(hello["result"]["capabilities"], "skin.transfer")
            != hello["result"]["capabilities"].end(),
        "hello must advertise the native spatial skin transfer operation"
    );
    expect(hello["result"]["session_id"].is_string(), "hello must expose the Runtime session");
    expect(
        hello["result"]["live_asset_count"] == 0U,
        "a new Runtime must have an empty asset registry"
    );

    const auto parse_error_text = runtime.handle_message("{");
    expect(parse_error_text.has_value(), "invalid JSON must return a parse error");
    const Json parse_error = Json::parse(*parse_error_text);
    expect(parse_error["error"]["code"] == -32700, "parse error code must follow JSON-RPC");
    expect(parse_error["id"].is_null(), "parse errors must use a null id");

    const Json unknown = call(runtime, "missing.method");
    expect(unknown["error"]["code"] == -32601, "unknown methods must return -32601");

    const Json notification{
        {"jsonrpc", "2.0"},
        {"method", "runtime.hello"},
    };
    expect(
        !runtime.handle_message(notification.dump()).has_value(),
        "notifications must not produce a response"
    );
}

void registry_tests() {
    const std::filesystem::path path = temporary_asset(".glb");
    unified3d::runtime::AssetRegistry registry{"test-session"};

    const auto first = registry.load(path);
    expect(first.success(), "asset registry must load an existing GLB source");
    expect(!first.reused, "first load must allocate a resource");
    expect(first.asset->handle.identity.object_id == 1U, "first resource id must be one");
    expect(first.asset->handle.identity.generation == 1U, "first generation must be one");
    expect(first.asset->retain_count == 1U, "first load must retain once");
    expect(first.asset->provenance.producer == "asset.load", "load provenance must name its producer");
    expect(first.asset->provenance.source_uri.has_value(), "load provenance must preserve source URI");
    expect(registry.live_asset_count() == 1U, "one live asset must be registered");

    auto position_storage = std::make_shared<std::vector<std::byte>>(36U);
    auto index_storage = std::make_shared<std::vector<std::byte>>(12U);
    unified3d::adapters::DecodedAssetBuffers decoded{
        .adapter = "test-adapter",
        .joint_names = {},
        .primitives = {
            {
                .name = "triangle",
                .source_mesh_index = 0U,
                .source_primitive_index = 0U,
                .domain = unified3d::adapters::GeometryDomain::render_vertices,
                .buffers = {
                    .positions = {
                        .semantic = unified3d::geometry::VertexSemantic::position,
                        .view = {
                            .storage = position_storage,
                            .element_count = 3U,
                            .component_count = 3U,
                            .scalar_type = unified3d::geometry::ScalarType::float32,
                        },
                    },
                    .indices = unified3d::geometry::IndexBuffer{{
                        .storage = index_storage,
                        .element_count = 3U,
                        .component_count = 1U,
                        .scalar_type = unified3d::geometry::ScalarType::uint32,
                    }},
                    .influence_sets = {},
                    .max_influences = 0U,
                },
            },
        },
    };
    const auto registered = registry.register_buffers(first.asset->handle, std::move(decoded));
    expect(registered.success(), "decoded buffers must register under their asset owner");
    const auto vertex_handle = registered.asset->primitives[0].positions.handle;
    const auto index_handle = registered.asset->primitives[0].indices->handle;
    expect(registry.contains(vertex_handle), "registered vertex handle must resolve");
    expect(registry.contains(index_handle), "registered index handle must resolve");
    const auto updated = registry.update_provenance(
        first.asset->handle,
        unified3d::runtime::ProvenanceRecord{
            .producer = "asset.normalize_spatial",
            .operation_id = "normalize:test-session:1:1",
            .source_uri = path.generic_string(),
            .source_revision = std::nullopt,
            .parents = {first.asset->handle},
        }
    );
    expect(updated.success(), "asset provenance must be updatable by a Runtime operation");
    expect(
        updated.asset->provenance.producer == "asset.normalize_spatial",
        "updated asset provenance must name the producing operation"
    );
    expect(
        registry.find(first.asset->handle)->provenance.parents.size() == 1U,
        "updated asset provenance must retain its source parent"
    );

    const auto alternate = registry.load(path, "alternate-adapter");
    expect(alternate.success() && !alternate.reused, "a distinct backend must own a distinct asset resource");
    expect(alternate.asset->handle != first.asset->handle, "backend variants must not alias handles");
    expect(registry.live_asset_count() == 2U, "both backend variants must coexist in one Runtime");
    unified3d::adapters::DecodedAssetBuffers target_decoded{
        .adapter = "target-adapter",
        .primitives = {
            {
                .name = "target-triangle",
                .domain = unified3d::adapters::GeometryDomain::render_vertices,
                .buffers = {
                    .positions = {
                        .semantic = unified3d::geometry::VertexSemantic::position,
                        .view = {
                            .storage = position_storage,
                            .element_count = 3U,
                            .component_count = 3U,
                            .scalar_type = unified3d::geometry::ScalarType::float32,
                        },
                    },
                    .indices = unified3d::geometry::IndexBuffer{{
                        .storage = index_storage,
                        .element_count = 3U,
                        .component_count = 1U,
                        .scalar_type = unified3d::geometry::ScalarType::uint32,
                    }},
                },
            },
        },
    };
    const auto target_registered = registry.register_buffers(
        alternate.asset->handle,
        std::move(target_decoded)
    );
    expect(target_registered.success(), "target geometry buffers must register before transferred skin");
    unified3d::geometry::TransferredPrimitiveSkin transferred{
        .influence_sets = {{
            .joints = {
                .semantic = unified3d::geometry::VertexSemantic::joints,
                .view = {
                    .storage = std::make_shared<std::vector<std::byte>>(48U),
                    .element_count = 3U,
                    .component_count = 4U,
                    .scalar_type = unified3d::geometry::ScalarType::uint32,
                },
            },
            .weights = {
                .semantic = unified3d::geometry::VertexSemantic::weights,
                .view = {
                    .storage = std::make_shared<std::vector<std::byte>>(48U),
                    .element_count = 3U,
                    .component_count = 4U,
                    .scalar_type = unified3d::geometry::ScalarType::float32,
                },
            },
        }},
        .max_influences = 1U,
        .vertex_count = 3U,
    };
    const auto skin_registered = registry.register_transferred_skin(
        alternate.asset->handle,
        first.asset->handle,
        {"joint0"},
        {std::move(transferred)}
    );
    expect(skin_registered.success(), "transferred weights must become Runtime-owned resources");
    const auto transferred_handle = skin_registered.asset->primitives[0].influence_sets[0].handle;
    expect(registry.contains(transferred_handle), "transferred skin handle must resolve while its target is live");
    expect(
        skin_registered.asset->primitives[0].influence_sets[0].provenance.producer == "skin.transfer",
        "transferred skin provenance must name the producing operation"
    );
    expect(
        skin_registered.asset->primitives[0].influence_sets[0].provenance.parents.size() == 2U,
        "transferred skin provenance must retain donor and target-buffer parents"
    );
    expect(registry.release(alternate.asset->handle).released, "alternate backend resource must release independently");
    expect(!registry.contains(transferred_handle), "releasing the target must invalidate transferred skin resources");

    const auto second = registry.load(path);
    expect(second.success() && second.reused, "unchanged source must reuse its live handle");
    expect(second.asset->handle == first.asset->handle, "cache reuse must return the same handle");
    expect(second.asset->retain_count == 2U, "cache reuse must retain the resource");

    const auto retained = registry.release(first.asset->handle);
    expect(retained.success() && !retained.released, "first release must preserve a second reference");
    expect(retained.remaining_references == 1U, "one retained reference must remain");
    const auto released = registry.release(first.asset->handle);
    expect(released.success() && released.released, "last release must destroy the resource");
    expect(registry.live_asset_count() == 0U, "released asset must leave the live registry");
    expect(!registry.find(first.asset->handle).has_value(), "released handle must not resolve");
    expect(!registry.contains(vertex_handle), "releasing an asset must invalidate its vertex buffers");
    expect(!registry.contains(index_handle), "releasing an asset must invalidate its index buffers");
    expect(!registry.release(first.asset->handle).success(), "stale handle must be rejected");

    const auto replacement = registry.load(path);
    expect(replacement.success(), "released slot must be reusable");
    expect(replacement.asset->handle.identity.object_id == 1U, "registry should reuse the free slot");
    expect(replacement.asset->handle.identity.generation == 2U, "slot reuse must advance generation");
    expect(
        replacement.asset->handle.encode() != first.asset->handle.encode(),
        "generation must make a stale encoded handle distinct"
    );

    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

void asset_rpc_tests() {
    const std::filesystem::path path = temporary_gltf_asset();
    unified3d::runtime::Runtime runtime;
    const Json first = call(runtime, "asset.load", Json{{"path", path.generic_string()}});
    const Json asset = first["result"]["asset"];
    expect(first["result"]["reused"] == false, "first RPC load must not report cache reuse");
    expect(asset["kind"] == "3D_ASSET", "asset RPC must return a typed handle");
    expect(asset["generation"] == 1U, "asset RPC must expose handle generation");
    expect(asset["provenance"]["producer"] == "asset.load", "asset RPC must expose provenance");
    expect(asset["adapter"] == "cgltf/1.15", "asset RPC must expose the native adapter");
    expect(asset["buffer_coordinate_system"] == "RIGHT_HANDED_Y_UP", "buffers must expose their canonical axes");
    expect(asset["buffer_unit_meters"] == 1.0, "buffers must expose meters as their canonical unit");
    expect(asset["primitives"].size() == 1U, "asset RPC must expose one primitive resource set");
    expect(asset["primitives"][0]["positions"]["element_count"] == 3U, "position descriptor must preserve vertex count");
    expect(asset["primitives"][0]["indices"]["element_count"] == 3U, "index descriptor must preserve index count");
    expect(
        asset["canonical_geometry_fingerprint"]["algorithm"]
            == "triangle-position-soup-fnv1a64-v1",
        "asset RPC must expose the canonical geometry fingerprint"
    );
    expect(
        asset["canonical_geometry_fingerprint"]["triangle_count"] == 1U,
        "canonical geometry fingerprint must preserve triangle count"
    );

    const Json second = call(
        runtime,
        "asset.load",
        Json{{"path", path.generic_string()}, {"backend", "cgltf"}}
    );
    expect(second["result"]["reused"] == true, "auto and explicit selection of the same backend must reuse unchanged source");
    expect(second["result"]["asset"]["id"] == asset["id"], "RPC reuse must preserve handle id");
    expect(second["result"]["asset"]["retain_count"] == 2U, "RPC reuse must expose retain count");

    const Json retained = call(runtime, "asset.release", Json{{"asset", asset}});
    expect(retained["result"]["released"] == false, "first RPC release must preserve one reference");
    const Json released = call(runtime, "asset.release", Json{{"asset", asset}});
    expect(released["result"]["released"] == true, "second RPC release must release the asset");
    const Json stale = call(runtime, "asset.release", Json{{"asset", asset}});
    expect(stale["error"]["code"] == -32020, "stale RPC handle must return -32020");

    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

#if defined(_WIN32)
bool pipe_write_all(const HANDLE pipe, const std::string_view value) {
    DWORD written = 0U;
    return WriteFile(
        pipe,
        value.data(),
        static_cast<DWORD>(value.size()),
        &written,
        nullptr
    ) != 0 && written == value.size();
}

std::string pipe_read_line(const HANDLE pipe) {
    std::string result;
    char current{};
    while (true) {
        DWORD received = 0U;
        if (ReadFile(pipe, &current, 1U, &received, nullptr) == 0 || received == 0U) {
            throw std::runtime_error("Named Pipe closed before a complete response.");
        }
        if (current == '\n') {
            return result;
        }
        result.push_back(current);
    }
}

void named_pipe_test() {
    const std::string name = R"(\\.\pipe\Unified3D.Runtime.Test.)"
        + std::to_string(GetCurrentProcessId());
    unified3d::runtime::Runtime runtime;
    int server_exit = -1;
    std::thread server{[&] { server_exit = unified3d::runtime::run_named_pipe(runtime, name); }};

    HANDLE pipe = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 100 && pipe == INVALID_HANDLE_VALUE; ++attempt) {
        pipe = CreateFileA(
            name.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0U,
            nullptr,
            OPEN_EXISTING,
            0U,
            nullptr
        );
        if (pipe == INVALID_HANDLE_VALUE) {
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
        }
    }
    if (pipe == INVALID_HANDLE_VALUE) {
        server.detach();
        throw std::runtime_error("Cannot connect to the Runtime Named Pipe test instance.");
    }

    expect(
        pipe_write_all(pipe, R"({"jsonrpc":"2.0","id":1,"method":"runtime.hello"})" "\n"),
        "Named Pipe client must send hello"
    );
    const Json hello = Json::parse(pipe_read_line(pipe));
    expect(hello["result"]["capabilities"].is_array(), "Named Pipe must use the shared dispatcher");
    expect(
        pipe_write_all(pipe, R"({"jsonrpc":"2.0","id":2,"method":"runtime.shutdown"})" "\n"),
        "Named Pipe client must send shutdown"
    );
    const Json shutdown = Json::parse(pipe_read_line(pipe));
    expect(shutdown["result"]["shutdown"] == true, "Named Pipe must acknowledge shutdown");
    CloseHandle(pipe);
    server.join();
    expect(server_exit == 0, "Named Pipe server must exit cleanly");
}
#else
void named_pipe_test() {
    expect(!unified3d::runtime::named_pipe_supported(), "non-Windows build must report no Named Pipe support");
}
#endif

void validation_tests(const Json& fbx) {
    unified3d::runtime::Runtime runtime;
    const Json valid = call(runtime, "analysis.validate", Json{{"analysis", fbx}});
    expect(valid["result"]["valid"] == true, "the canonical FBX fixture must validate");
    expect(
        valid["result"]["diagnostics"].size() == 1U,
        "unknown coordinates must remain a non-fatal diagnostic"
    );
    expect(
        valid["result"]["diagnostics"][0]["code"] == "COORDINATE_SYSTEM_UNKNOWN",
        "native validation must preserve the Python diagnostic code"
    );

    Json incomplete = fbx;
    incomplete["geometry"].erase("triangle_count");
    const Json invalid = call(
        runtime,
        "analysis.validate",
        Json{{"analysis", std::move(incomplete)}}
    );
    expect(invalid["result"]["valid"] == false, "missing required fields must be rejected");
    expect(
        invalid["result"]["diagnostics"][0]["path"] == "$.geometry.triangle_count",
        "validation diagnostics must point at the missing field"
    );
}

void comparison_tests(const Json& fbx, const Json& glb) {
    unified3d::runtime::Runtime runtime;
    const Json response = call(
        runtime,
        "analysis.compare",
        Json{{"a", fbx}, {"b", glb}},
        42
    );
    expect(response["id"] == 42, "comparison must preserve the numeric request id");
    const Json& result = response["result"];
    expect(
        result["schema"] == "unified3d.analysis-comparison/1.0-rc1",
        "comparison must emit the RC1 comparison schema"
    );
    expect(result["inputs"]["a"] == fbx, "comparison must preserve canonical input A");
    expect(result["inputs"]["b"] == glb, "comparison must preserve canonical input B");

    const Json& comparison = result["comparison"];
    expect(comparison["same_mesh_count"] == true, "both thief assets must have ten meshes");
    expect(
        comparison["index_transfer_ruled_out_by_triangle_count"] == true,
        "triangle mismatch must rule out an index transfer"
    );
    expect(
        std::abs(comparison["triangle_ratio_b_over_a"].get<double>()
            - 39.09874348269043) <= 1.0e-12,
        "the Runtime ratio must match the Core and Python values"
    );
    expect(
        comparison["rig_donor_geometry_target_pattern"]["donor"] == "a",
        "the animated FBX must be selected as rig donor"
    );
    expect(
        comparison["rig_donor_geometry_target_pattern"]["target"] == "b",
        "the dense GLB must be selected as geometry target"
    );
    expect(
        comparison["compatibility"]["classification"] == "ADVANCED_TRANSFER_REQUIRED",
        "the thief pair must require advanced transfer"
    );
    expect(
        comparison["compatibility"]["levels"].size() == 8U,
        "all compatibility levels zero through seven must be serialized"
    );
    expect(
        comparison["compatibility"]["levels"][3]["evidence"]["a_mesh_count"].is_number_unsigned(),
        "numeric evidence must remain numeric at the RPC boundary"
    );
    expect(
        comparison["compatibility"]["levels"][0]["evidence"]["normalized"].is_boolean(),
        "boolean evidence must remain boolean at the RPC boundary"
    );

    const Json reverse = call(
        runtime,
        "analysis.compare",
        Json{{"a", glb}, {"b", fbx}}
    );
    expect(
        reverse["result"]["comparison"]["rig_donor_geometry_target_pattern"]["donor"] == "b",
        "reverse comparison must still identify FBX as the donor"
    );

    Json invalid_fbx = fbx;
    invalid_fbx["skin"]["present"] = false;
    const Json rejected = call(
        runtime,
        "analysis.compare",
        Json{{"a", std::move(invalid_fbx)}, {"b", glb}}
    );
    expect(rejected["error"]["code"] == -32010, "invalid analysis input must return -32010");
    expect(
        rejected["error"]["data"]["diagnostics"][0]["path"]
            .get<std::string>().starts_with("$.params.a."),
        "comparison validation errors must identify input A"
    );
}

void stdio_lifecycle_test() {
    unified3d::runtime::Runtime runtime;
    std::istringstream input{
        R"({"jsonrpc":"2.0","id":1,"method":"runtime.hello"})" "\n"
        R"({"jsonrpc":"2.0","id":2,"method":"runtime.shutdown"})" "\n"
        R"({"jsonrpc":"2.0","id":3,"method":"runtime.hello"})" "\n"
    };
    std::ostringstream output;
    const int exit_code = unified3d::runtime::run_stdio(runtime, input, output);
    expect(exit_code == 0, "stdio server must exit successfully after shutdown");
    expect(runtime.shutdown_requested(), "shutdown must change runtime state");

    std::istringstream lines{output.str()};
    std::string first;
    std::string second;
    std::string unexpected;
    expect(static_cast<bool>(std::getline(lines, first)), "stdio must emit the hello response");
    expect(static_cast<bool>(std::getline(lines, second)), "stdio must emit the shutdown response");
    expect(!std::getline(lines, unexpected), "stdio must stop before processing later messages");
    expect(Json::parse(second)["result"]["shutdown"] == true, "shutdown acknowledgement must be explicit");
}

}  // namespace

int main() {
    try {
        const Json fbx = read_json_fixture("thief-fbx.analysis-1.0-rc1.json");
        const Json glb = read_json_fixture("thief-glb.analysis-1.0-rc1.json");
        protocol_tests();
        registry_tests();
        asset_rpc_tests();
        validation_tests(fbx);
        comparison_tests(fbx, glb);
        stdio_lifecycle_test();
        named_pipe_test();
    } catch (const std::exception& error) {
        std::cerr << "UNCAUGHT: " << error.what() << '\n';
        return EXIT_FAILURE;
    }

    if (failures != 0) {
        std::cerr << failures << " runtime test(s) failed.\n";
        return EXIT_FAILURE;
    }
    std::cout << "All Unified3D Runtime tests passed.\n";
    return EXIT_SUCCESS;
}
