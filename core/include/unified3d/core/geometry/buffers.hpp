#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include <unified3d/core/diagnostic.hpp>

namespace unified3d::geometry {

enum class ScalarType {
    float32,
    float64,
    uint16,
    uint32,
};

enum class VertexSemantic {
    position,
    joints,
    weights,
};

using ImmutableBytes = std::shared_ptr<const std::vector<std::byte>>;

struct BufferView {
    ImmutableBytes storage;
    std::size_t byte_offset{};
    std::size_t byte_stride{};
    std::uint64_t element_count{};
    std::uint32_t component_count{};
    ScalarType scalar_type{ScalarType::float32};
};

struct VertexAttributeBuffer {
    VertexSemantic semantic{VertexSemantic::position};
    BufferView view;
};

struct IndexBuffer {
    BufferView view;
};

struct SkinInfluenceSet {
    VertexAttributeBuffer joints;
    VertexAttributeBuffer weights;
};

struct PrimitiveBuffers {
    VertexAttributeBuffer positions;
    std::optional<IndexBuffer> indices;
    std::vector<SkinInfluenceSet> influence_sets;
    std::uint32_t max_influences{};
};

using SkinTransferBuffers = PrimitiveBuffers;

[[nodiscard]] std::size_t scalar_size(ScalarType type) noexcept;

[[nodiscard]] ValidationResult validate_buffer_view(
    const BufferView& view,
    std::string_view path
);

[[nodiscard]] ValidationResult validate_skin_transfer_buffers(
    const SkinTransferBuffers& buffers
);

[[nodiscard]] ValidationResult validate_primitive_buffers(
    const PrimitiveBuffers& buffers
);

}  // namespace unified3d::geometry
