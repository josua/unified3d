#include <unified3d/core/geometry/buffers.hpp>

#include <limits>
#include <iterator>
#include <string>
#include <string_view>

namespace unified3d::geometry {
namespace {

void add_error(
    ValidationResult& result,
    std::string code,
    std::string message,
    std::string path
) {
    result.diagnostics.push_back(
        Diagnostic{
            .severity = DiagnosticSeverity::error,
            .code = std::move(code),
            .message = std::move(message),
            .path = std::move(path),
        }
    );
}

void append(ValidationResult& destination, ValidationResult source) {
    destination.diagnostics.insert(
        destination.diagnostics.end(),
        std::make_move_iterator(source.diagnostics.begin()),
        std::make_move_iterator(source.diagnostics.end())
    );
}

bool is_float(const ScalarType type) {
    return type == ScalarType::float32 || type == ScalarType::float64;
}

bool is_unsigned_integer(const ScalarType type) {
    return type == ScalarType::uint16 || type == ScalarType::uint32;
}

void validate_attribute(
    ValidationResult& result,
    const VertexAttributeBuffer& attribute,
    const VertexSemantic expected_semantic,
    const std::uint32_t expected_components,
    const std::string_view path
) {
    append(result, validate_buffer_view(attribute.view, path));
    if (attribute.semantic != expected_semantic) {
        add_error(
            result,
            "BUFFER_SEMANTIC",
            "The attribute semantic does not match its role.",
            std::string{path} + ".semantic"
        );
    }
    if (attribute.view.component_count != expected_components) {
        add_error(
            result,
            "BUFFER_COMPONENTS",
            "The attribute has an invalid component count.",
            std::string{path} + ".component_count"
        );
    }
}

}  // namespace

std::size_t scalar_size(const ScalarType type) noexcept {
    switch (type) {
        case ScalarType::float32:
        case ScalarType::uint32:
            return 4U;
        case ScalarType::float64:
            return 8U;
        case ScalarType::uint16:
            return 2U;
    }
    return 0U;
}

ValidationResult validate_buffer_view(
    const BufferView& view,
    const std::string_view path
) {
    ValidationResult result;
    const std::string root = path.empty() ? "$" : std::string{path};
    if (!view.storage) {
        add_error(result, "BUFFER_STORAGE", "Buffer storage is required.", root);
        return result;
    }
    if (view.element_count == 0U) {
        add_error(
            result,
            "BUFFER_ELEMENT_COUNT",
            "A geometry buffer must contain at least one element.",
            root + ".element_count"
        );
    }
    if (view.component_count == 0U) {
        add_error(
            result,
            "BUFFER_COMPONENT_COUNT",
            "A buffer element must contain at least one component.",
            root + ".component_count"
        );
        return result;
    }

    const std::size_t component_size = scalar_size(view.scalar_type);
    if (view.component_count > std::numeric_limits<std::size_t>::max() / component_size) {
        add_error(result, "BUFFER_OVERFLOW", "Buffer element size overflows.", root);
        return result;
    }
    const std::size_t element_size = component_size
        * static_cast<std::size_t>(view.component_count);
    const std::size_t stride = view.byte_stride == 0U ? element_size : view.byte_stride;
    if (stride < element_size) {
        add_error(
            result,
            "BUFFER_STRIDE",
            "Byte stride must be zero (packed) or at least the element size.",
            root + ".byte_stride"
        );
        return result;
    }
    if (view.element_count == 0U) {
        return result;
    }
    const std::uint64_t last_index = view.element_count - 1U;
    if (last_index > std::numeric_limits<std::size_t>::max() / stride) {
        add_error(result, "BUFFER_OVERFLOW", "Buffer byte range overflows.", root);
        return result;
    }
    const std::size_t last_offset = static_cast<std::size_t>(last_index) * stride;
    if (view.byte_offset > std::numeric_limits<std::size_t>::max() - last_offset
        || view.byte_offset + last_offset
            > std::numeric_limits<std::size_t>::max() - element_size) {
        add_error(result, "BUFFER_OVERFLOW", "Buffer byte range overflows.", root);
        return result;
    }
    const std::size_t required_size = view.byte_offset + last_offset + element_size;
    if (required_size > view.storage->size()) {
        add_error(
            result,
            "BUFFER_BOUNDS",
            "Buffer view exceeds its immutable storage.",
            root
        );
    }
    return result;
}

ValidationResult validate_primitive_buffers(const PrimitiveBuffers& buffers) {
    ValidationResult result;
    validate_attribute(
        result,
        buffers.positions,
        VertexSemantic::position,
        3U,
        "$.positions"
    );
    if (!is_float(buffers.positions.view.scalar_type)) {
        add_error(
            result,
            "POSITION_SCALAR_TYPE",
            "Positions require float32 or float64 components.",
            "$.positions.scalar_type"
        );
    }

    if (buffers.indices.has_value()) {
        append(result, validate_buffer_view(buffers.indices->view, "$.indices"));
        if (buffers.indices->view.component_count != 1U) {
            add_error(
                result,
                "INDEX_COMPONENTS",
                "Indices require one component per element.",
                "$.indices.component_count"
            );
        }
        if (!is_unsigned_integer(buffers.indices->view.scalar_type)) {
            add_error(
                result,
                "INDEX_SCALAR_TYPE",
                "Indices require uint16 or uint32 components.",
                "$.indices.scalar_type"
            );
        }
    }

    for (std::size_t index = 0U; index < buffers.influence_sets.size(); ++index) {
        const std::string root = "$.influence_sets[" + std::to_string(index) + "]";
        const auto& influence_set = buffers.influence_sets[index];
        validate_attribute(
            result,
            influence_set.joints,
            VertexSemantic::joints,
            4U,
            root + ".joints"
        );
        validate_attribute(
            result,
            influence_set.weights,
            VertexSemantic::weights,
            4U,
            root + ".weights"
        );
        if (!is_unsigned_integer(influence_set.joints.view.scalar_type)) {
            add_error(
                result,
                "JOINT_SCALAR_TYPE",
                "Joint indices require uint16 or uint32 components.",
                root + ".joints.scalar_type"
            );
        }
        if (!is_float(influence_set.weights.view.scalar_type)) {
            add_error(
                result,
                "WEIGHT_SCALAR_TYPE",
                "Skin weights require float32 or float64 components.",
                root + ".weights.scalar_type"
            );
        }
        if (influence_set.joints.view.element_count
                != buffers.positions.view.element_count
            || influence_set.weights.view.element_count
                != buffers.positions.view.element_count) {
            add_error(
                result,
                "SKIN_VERTEX_COUNT",
                "Each JOINTS_n/WEIGHTS_n set must match the position count.",
                root
            );
        }
    }

    if (buffers.max_influences == 0U && !buffers.influence_sets.empty()) {
        add_error(
            result,
            "MAX_INFLUENCES",
            "Influence buffers require at least one non-zero influence.",
            "$.max_influences"
        );
    }
    const std::size_t required_sets = buffers.max_influences == 0U
        ? 0U
        : (static_cast<std::size_t>(buffers.max_influences) + 3U) / 4U;
    if (buffers.influence_sets.size() < required_sets) {
        add_error(
            result,
            "INFLUENCE_SET_CAPACITY",
            "Influence sets must provide at least ceil(max_influences / 4) JOINTS_n/WEIGHTS_n pairs.",
            "$.influence_sets"
        );
    }
    return result;
}

ValidationResult validate_skin_transfer_buffers(const SkinTransferBuffers& buffers) {
    ValidationResult result = validate_primitive_buffers(buffers);
    if (buffers.max_influences == 0U || buffers.influence_sets.empty()) {
        add_error(
            result,
            "SKIN_BUFFERS_REQUIRED",
            "Skin-transfer buffers require at least one JOINTS_n/WEIGHTS_n pair.",
            "$.influence_sets"
        );
    }
    return result;
}

}  // namespace unified3d::geometry
