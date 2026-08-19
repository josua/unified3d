#include <unified3d/runtime/runtime.hpp>

#include "json/analysis_codec.hpp"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <istream>
#include <memory>
#include <ostream>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include <unified3d/core/version.hpp>
#include <unified3d/operations/analysis/compare_analysis_records.hpp>

namespace unified3d::runtime {
namespace {

using Json = nlohmann::json;
using json_codec::DecodeAnalysisResult;

constexpr std::size_t maximum_control_message_bytes = 4U * 1024U * 1024U;

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
            Json result{
                {"runtime_version", "0.2.0-dev"},
                {"core_version", unified3d::version},
                {"protocol_version", "1.0"},
                {"analysis_schema", unified3d::analysis_schema},
                {"analysis_comparison_schema", unified3d::analysis_comparison_schema},
                {
                    "capabilities",
                    Json::array(
                        {
                            "transport.stdio",
                            "analysis.validate",
                            "analysis.compare",
                        }
                    )
                },
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
