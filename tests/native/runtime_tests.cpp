#include <unified3d/runtime/runtime.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

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
        hello["result"]["capabilities"].size() == 3U,
        "hello must advertise all initial runtime capabilities"
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
        comparison["compatibility"]["levels"].size() == 7U,
        "all compatibility levels zero through six must be serialized"
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
        validation_tests(fbx);
        comparison_tests(fbx, glb);
        stdio_lifecycle_test();
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
