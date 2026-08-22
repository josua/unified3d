#include <unified3d/core/geometry/canonical_fingerprint.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <compare>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <utility>

namespace unified3d::geometry {
namespace {

struct QuantizedPosition {
    std::array<std::int64_t, 3> values{};

    [[nodiscard]] auto operator<=>(const QuantizedPosition&) const = default;
};

struct CanonicalTriangle {
    std::array<QuantizedPosition, 3> vertices{};

    [[nodiscard]] auto operator<=>(const CanonicalTriangle&) const = default;
};

void add_error(
    CanonicalFingerprintResult& result,
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

template <typename T>
std::optional<T> read_scalar(const BufferView& view, const std::uint64_t element,
                             const std::uint32_t component) {
    const std::size_t component_size = sizeof(T);
    const std::size_t element_size = component_size
        * static_cast<std::size_t>(view.component_count);
    const std::size_t stride = view.byte_stride == 0U ? element_size : view.byte_stride;
    if (!view.storage || element >= view.element_count || component >= view.component_count
        || element > std::numeric_limits<std::size_t>::max() / stride) {
        return std::nullopt;
    }
    const std::size_t offset = view.byte_offset
        + static_cast<std::size_t>(element) * stride
        + static_cast<std::size_t>(component) * component_size;
    if (offset > view.storage->size() || view.storage->size() - offset < component_size) {
        return std::nullopt;
    }
    T value{};
    std::memcpy(&value, view.storage->data() + offset, component_size);
    return value;
}

std::optional<double> read_position_component(
    const BufferView& view,
    const std::uint64_t element,
    const std::uint32_t component
) {
    if (view.scalar_type == ScalarType::float32) {
        const auto value = read_scalar<float>(view, element, component);
        return value.has_value() ? std::optional<double>{*value} : std::nullopt;
    }
    if (view.scalar_type == ScalarType::float64) {
        return read_scalar<double>(view, element, component);
    }
    return std::nullopt;
}

std::optional<std::uint64_t> read_index(
    const BufferView& view,
    const std::uint64_t element
) {
    if (view.scalar_type == ScalarType::uint16) {
        const auto value = read_scalar<std::uint16_t>(view, element, 0U);
        return value.has_value() ? std::optional<std::uint64_t>{*value} : std::nullopt;
    }
    if (view.scalar_type == ScalarType::uint32) {
        const auto value = read_scalar<std::uint32_t>(view, element, 0U);
        return value.has_value() ? std::optional<std::uint64_t>{*value} : std::nullopt;
    }
    return std::nullopt;
}

std::optional<QuantizedPosition> quantized_position(
    const BufferView& positions,
    const std::uint64_t vertex,
    const double tolerance
) {
    QuantizedPosition result;
    for (std::uint32_t component = 0U; component < 3U; ++component) {
        const auto value = read_position_component(positions, vertex, component);
        if (!value.has_value() || !std::isfinite(*value)) {
            return std::nullopt;
        }
        const double scaled = *value / tolerance;
        if (!std::isfinite(scaled)
            || std::abs(scaled) > static_cast<double>(std::numeric_limits<std::int64_t>::max())) {
            return std::nullopt;
        }
        result.values[component] = static_cast<std::int64_t>(std::llround(scaled));
    }
    return result;
}

void hash_u64(std::uint64_t& hash, const std::uint64_t value) {
    constexpr std::uint64_t prime = 1099511628211ULL;
    for (std::uint32_t byte = 0U; byte < 8U; ++byte) {
        hash ^= (value >> (byte * 8U)) & 0xffU;
        hash *= prime;
    }
}

std::string hex_digest(const std::uint64_t value) {
    std::ostringstream output;
    output << std::hex << std::setfill('0') << std::setw(16) << value;
    return output.str();
}

}  // namespace

bool CanonicalFingerprintResult::success() const noexcept {
    return fingerprint.has_value() && std::ranges::none_of(
        diagnostics,
        [](const Diagnostic& diagnostic) {
            return diagnostic.severity == DiagnosticSeverity::error;
        }
    );
}

CanonicalFingerprintResult fingerprint_triangle_position_soup(
    const std::span<const PrimitiveBuffers> primitives,
    const double position_tolerance_m
) {
    CanonicalFingerprintResult result;
    if (!std::isfinite(position_tolerance_m) || position_tolerance_m <= 0.0) {
        add_error(result, "FINGERPRINT_TOLERANCE", "Position tolerance must be finite and positive.");
        return result;
    }
    if (primitives.empty()) {
        add_error(result, "FINGERPRINT_PRIMITIVES", "At least one primitive is required.");
        return result;
    }

    std::vector<CanonicalTriangle> triangles;
    for (std::size_t primitive_index = 0U; primitive_index < primitives.size(); ++primitive_index) {
        const PrimitiveBuffers& primitive = primitives[primitive_index];
        const ValidationResult validation = validate_primitive_buffers(primitive);
        if (!validation.valid()) {
            add_error(
                result,
                "FINGERPRINT_BUFFERS_INVALID",
                "Cannot fingerprint invalid primitive buffers.",
                "$.primitives[" + std::to_string(primitive_index) + "]"
            );
            return result;
        }
        const std::uint64_t index_count = primitive.indices.has_value()
            ? primitive.indices->view.element_count
            : primitive.positions.view.element_count;
        if (index_count == 0U || index_count % 3U != 0U) {
            add_error(
                result,
                "FINGERPRINT_TRIANGLES",
                "Canonical fingerprinting requires a non-empty triangle index domain.",
                "$.primitives[" + std::to_string(primitive_index) + "]"
            );
            return result;
        }
        const std::uint64_t triangle_count = index_count / 3U;
        if (triangle_count > std::numeric_limits<std::size_t>::max() - triangles.size()) {
            add_error(result, "FINGERPRINT_OVERFLOW", "Triangle count exceeds addressable memory.");
            return result;
        }
        triangles.reserve(triangles.size() + static_cast<std::size_t>(triangle_count));
        for (std::uint64_t triangle_index = 0U; triangle_index < triangle_count; ++triangle_index) {
            CanonicalTriangle triangle;
            for (std::uint32_t lane = 0U; lane < 3U; ++lane) {
                const std::uint64_t source_index = triangle_index * 3U + lane;
                const auto vertex = primitive.indices.has_value()
                    ? read_index(primitive.indices->view, source_index)
                    : std::optional<std::uint64_t>{source_index};
                if (!vertex.has_value() || *vertex >= primitive.positions.view.element_count) {
                    add_error(result, "FINGERPRINT_INDEX", "Triangle index is outside POSITION bounds.");
                    return result;
                }
                const auto position = quantized_position(
                    primitive.positions.view, *vertex, position_tolerance_m
                );
                if (!position.has_value()) {
                    add_error(result, "FINGERPRINT_POSITION", "A position is unreadable, non-finite, or outside the quantization range.");
                    return result;
                }
                triangle.vertices[lane] = *position;
            }
            std::ranges::sort(triangle.vertices);
            triangles.push_back(triangle);
        }
    }
    std::ranges::sort(triangles);

    std::uint64_t hash = 14695981039346656037ULL;
    hash_u64(hash, 0x553344465052494eULL);  // "U3DFPRIN"
    hash_u64(hash, static_cast<std::uint64_t>(triangles.size()));
    for (const CanonicalTriangle& triangle : triangles) {
        for (const QuantizedPosition& vertex : triangle.vertices) {
            for (const std::int64_t component : vertex.values) {
                hash_u64(hash, static_cast<std::uint64_t>(component));
            }
        }
    }
    result.fingerprint = CanonicalGeometryFingerprint{
        .algorithm = "triangle-position-soup-fnv1a64-v1",
        .digest = hex_digest(hash),
        .triangle_count = static_cast<std::uint64_t>(triangles.size()),
        .position_tolerance_m = position_tolerance_m,
    };
    return result;
}

}  // namespace unified3d::geometry
