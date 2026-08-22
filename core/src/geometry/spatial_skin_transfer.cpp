#include <unified3d/core/geometry/spatial_skin_transfer.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>
#include <utility>

namespace unified3d::geometry {
namespace {

struct Vec3 {
    double x{};
    double y{};
    double z{};
};

struct Aabb {
    Vec3 minimum{
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
    };
    Vec3 maximum{
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
    };
};

struct SourceVertex {
    Vec3 position;
    std::uint32_t primitive{};
    std::uint64_t local_vertex{};
};

struct Triangle {
    std::array<std::uint32_t, 3> vertices{};
};

struct BvhNode {
    Aabb bounds;
    std::uint32_t begin{};
    std::uint32_t count{};
    std::uint32_t left{};
    std::uint32_t right{};

    [[nodiscard]] bool leaf() const noexcept { return count != 0U; }
};

struct ClosestPoint {
    Vec3 point;
    std::array<double, 3> barycentric{};
    double distance_squared{std::numeric_limits<double>::infinity()};
};

struct WeightedJoint {
    std::uint32_t joint{};
    double weight{};
};

void add_error(SpatialSkinTransferResult& result, std::string code, std::string message) {
    result.diagnostics.push_back(Diagnostic{
        .severity = DiagnosticSeverity::error,
        .code = std::move(code),
        .message = std::move(message),
        .path = "$",
    });
}

template <typename T>
std::optional<T> read_scalar(
    const BufferView& view,
    const std::uint64_t element,
    const std::uint32_t component
) {
    const std::size_t element_size = sizeof(T) * view.component_count;
    const std::size_t stride = view.byte_stride == 0U ? element_size : view.byte_stride;
    if (!view.storage || element >= view.element_count || component >= view.component_count
        || element > std::numeric_limits<std::size_t>::max() / stride) {
        return std::nullopt;
    }
    const std::size_t offset = view.byte_offset + static_cast<std::size_t>(element) * stride
        + static_cast<std::size_t>(component) * sizeof(T);
    if (offset > view.storage->size() || view.storage->size() - offset < sizeof(T)) {
        return std::nullopt;
    }
    T value{};
    std::memcpy(&value, view.storage->data() + offset, sizeof(T));
    return value;
}

std::optional<double> read_float(
    const BufferView& view,
    const std::uint64_t element,
    const std::uint32_t component
) {
    if (view.scalar_type == ScalarType::float32) {
        const auto value = read_scalar<float>(view, element, component);
        return value ? std::optional<double>{*value} : std::nullopt;
    }
    if (view.scalar_type == ScalarType::float64) {
        return read_scalar<double>(view, element, component);
    }
    return std::nullopt;
}

std::optional<std::uint32_t> read_uint(
    const BufferView& view,
    const std::uint64_t element,
    const std::uint32_t component
) {
    if (view.scalar_type == ScalarType::uint16) {
        const auto value = read_scalar<std::uint16_t>(view, element, component);
        return value ? std::optional<std::uint32_t>{*value} : std::nullopt;
    }
    if (view.scalar_type == ScalarType::uint32) {
        return read_scalar<std::uint32_t>(view, element, component);
    }
    return std::nullopt;
}

Vec3 transform_point(const Matrix4d& matrix, const Vec3 point) {
    const double w = matrix[3] * point.x + matrix[7] * point.y
        + matrix[11] * point.z + matrix[15];
    const double inverse_w = std::abs(w) > 1.0e-15 ? 1.0 / w : 1.0;
    return {
        (matrix[0] * point.x + matrix[4] * point.y + matrix[8] * point.z + matrix[12]) * inverse_w,
        (matrix[1] * point.x + matrix[5] * point.y + matrix[9] * point.z + matrix[13]) * inverse_w,
        (matrix[2] * point.x + matrix[6] * point.y + matrix[10] * point.z + matrix[14]) * inverse_w,
    };
}

std::optional<Vec3> read_position(
    const VertexAttributeBuffer& positions,
    const std::uint64_t vertex,
    const Matrix4d& transform
) {
    const auto x = read_float(positions.view, vertex, 0U);
    const auto y = read_float(positions.view, vertex, 1U);
    const auto z = read_float(positions.view, vertex, 2U);
    if (!x || !y || !z || !std::isfinite(*x) || !std::isfinite(*y) || !std::isfinite(*z)) {
        return std::nullopt;
    }
    const Vec3 result = transform_point(transform, {*x, *y, *z});
    if (!std::isfinite(result.x) || !std::isfinite(result.y) || !std::isfinite(result.z)) {
        return std::nullopt;
    }
    return result;
}

Vec3 subtract(const Vec3 a, const Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 add(const Vec3 a, const Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 multiply(const Vec3 value, const double scale) { return {value.x * scale, value.y * scale, value.z * scale}; }
double dot(const Vec3 a, const Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

void expand(Aabb& bounds, const Vec3 point) {
    bounds.minimum.x = std::min(bounds.minimum.x, point.x);
    bounds.minimum.y = std::min(bounds.minimum.y, point.y);
    bounds.minimum.z = std::min(bounds.minimum.z, point.z);
    bounds.maximum.x = std::max(bounds.maximum.x, point.x);
    bounds.maximum.y = std::max(bounds.maximum.y, point.y);
    bounds.maximum.z = std::max(bounds.maximum.z, point.z);
}

double distance_squared(const Aabb& bounds, const Vec3 point) {
    const auto axis = [](const double value, const double minimum, const double maximum) {
        if (value < minimum) return minimum - value;
        if (value > maximum) return value - maximum;
        return 0.0;
    };
    const double x = axis(point.x, bounds.minimum.x, bounds.maximum.x);
    const double y = axis(point.y, bounds.minimum.y, bounds.maximum.y);
    const double z = axis(point.z, bounds.minimum.z, bounds.maximum.z);
    return x * x + y * y + z * z;
}

ClosestPoint closest_point_on_triangle(const Vec3 point, const Vec3 a, const Vec3 b, const Vec3 c) {
    const Vec3 ab = subtract(b, a);
    const Vec3 ac = subtract(c, a);
    const Vec3 ap = subtract(point, a);
    const double d1 = dot(ab, ap);
    const double d2 = dot(ac, ap);
    ClosestPoint result;
    if (d1 <= 0.0 && d2 <= 0.0) {
        result.point = a;
        result.barycentric = {1.0, 0.0, 0.0};
    } else {
        const Vec3 bp = subtract(point, b);
        const double d3 = dot(ab, bp);
        const double d4 = dot(ac, bp);
        if (d3 >= 0.0 && d4 <= d3) {
            result.point = b;
            result.barycentric = {0.0, 1.0, 0.0};
        } else {
            const double vc = d1 * d4 - d3 * d2;
            if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) {
                const double v = d1 / (d1 - d3);
                result.point = add(a, multiply(ab, v));
                result.barycentric = {1.0 - v, v, 0.0};
            } else {
                const Vec3 cp = subtract(point, c);
                const double d5 = dot(ab, cp);
                const double d6 = dot(ac, cp);
                if (d6 >= 0.0 && d5 <= d6) {
                    result.point = c;
                    result.barycentric = {0.0, 0.0, 1.0};
                } else {
                    const double vb = d5 * d2 - d1 * d6;
                    if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) {
                        const double w = d2 / (d2 - d6);
                        result.point = add(a, multiply(ac, w));
                        result.barycentric = {1.0 - w, 0.0, w};
                    } else {
                        const double va = d3 * d6 - d5 * d4;
                        if (va <= 0.0 && d4 - d3 >= 0.0 && d5 - d6 >= 0.0) {
                            const double w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
                            result.point = add(b, multiply(subtract(c, b), w));
                            result.barycentric = {0.0, 1.0 - w, w};
                        } else {
                            const double denominator = va + vb + vc;
                            if (std::abs(denominator) <= 1.0e-30) {
                                const auto da = dot(ap, ap);
                                const auto db = dot(bp, bp);
                                const auto dc = dot(cp, cp);
                                if (da <= db && da <= dc) {
                                    result.point = a;
                                    result.barycentric = {1.0, 0.0, 0.0};
                                } else if (db <= dc) {
                                    result.point = b;
                                    result.barycentric = {0.0, 1.0, 0.0};
                                } else {
                                    result.point = c;
                                    result.barycentric = {0.0, 0.0, 1.0};
                                }
                            }
                            else {
                                const double inverse = 1.0 / denominator;
                                const double v = vb * inverse;
                                const double w = vc * inverse;
                                result.point = add(a, add(multiply(ab, v), multiply(ac, w)));
                                result.barycentric = {1.0 - v - w, v, w};
                            }
                        }
                    }
                }
            }
        }
    }
    const Vec3 delta = subtract(point, result.point);
    result.distance_squared = dot(delta, delta);
    return result;
}

class TriangleBvh final {
public:
    TriangleBvh(const std::vector<SourceVertex>& vertices, const std::vector<Triangle>& triangles)
        : vertices_(vertices), triangles_(triangles), order_(triangles.size()) {
        std::iota(order_.begin(), order_.end(), 0U);
        nodes_.reserve(triangles.size() * 2U);
        build(0U, static_cast<std::uint32_t>(order_.size()));
    }

    std::pair<std::uint32_t, ClosestPoint> nearest(const Vec3 point) const {
        std::uint32_t best_triangle{};
        ClosestPoint best;
        // The tree is median-split and therefore logarithmic. A fixed traversal
        // stack avoids one heap allocation for every target vertex.
        std::array<std::uint32_t, 128U> stack{};
        std::size_t stack_size = 1U;
        stack[0] = 0U;
        const auto push = [&](const std::uint32_t node) {
            // Reaching this limit would require more triangles than can be
            // represented by the uint32 indices used by this implementation.
            if (stack_size < stack.size()) stack[stack_size++] = node;
        };
        while (stack_size != 0U) {
            const std::uint32_t node_index = stack[--stack_size];
            const BvhNode& node = nodes_[node_index];
            if (distance_squared(node.bounds, point) > best.distance_squared) continue;
            if (node.leaf()) {
                for (std::uint32_t offset = 0U; offset < node.count; ++offset) {
                    const std::uint32_t triangle_index = order_[node.begin + offset];
                    const Triangle& triangle = triangles_[triangle_index];
                    const ClosestPoint candidate = closest_point_on_triangle(
                        point,
                        vertices_[triangle.vertices[0]].position,
                        vertices_[triangle.vertices[1]].position,
                        vertices_[triangle.vertices[2]].position
                    );
                    if (candidate.distance_squared < best.distance_squared) {
                        best = candidate;
                        best_triangle = triangle_index;
                    }
                }
            } else {
                const double left_distance = distance_squared(nodes_[node.left].bounds, point);
                const double right_distance = distance_squared(nodes_[node.right].bounds, point);
                if (left_distance < right_distance) {
                    push(node.right);
                    push(node.left);
                } else {
                    push(node.left);
                    push(node.right);
                }
            }
        }
        return {best_triangle, best};
    }

private:
    std::uint32_t build(const std::uint32_t begin, const std::uint32_t end) {
        const std::uint32_t node_index = static_cast<std::uint32_t>(nodes_.size());
        nodes_.emplace_back();
        Aabb bounds;
        Aabb centroids;
        for (std::uint32_t offset = begin; offset < end; ++offset) {
            const Triangle& triangle = triangles_[order_[offset]];
            Vec3 centroid{};
            for (const std::uint32_t vertex : triangle.vertices) {
                const Vec3 position = vertices_[vertex].position;
                expand(bounds, position);
                centroid = add(centroid, multiply(position, 1.0 / 3.0));
            }
            expand(centroids, centroid);
        }
        const std::uint32_t count = end - begin;
        if (count <= 8U) {
            nodes_[node_index] = BvhNode{.bounds = bounds, .begin = begin, .count = count};
            return node_index;
        }
        const Vec3 extent = subtract(centroids.maximum, centroids.minimum);
        const std::uint32_t axis = extent.y > extent.x && extent.y >= extent.z ? 1U
            : (extent.z > extent.x ? 2U : 0U);
        const std::uint32_t middle = begin + count / 2U;
        const auto coordinate = [&](const std::uint32_t triangle_index) {
            const Triangle& triangle = triangles_[triangle_index];
            const Vec3 a = vertices_[triangle.vertices[0]].position;
            const Vec3 b = vertices_[triangle.vertices[1]].position;
            const Vec3 c = vertices_[triangle.vertices[2]].position;
            if (axis == 0U) return (a.x + b.x + c.x) / 3.0;
            if (axis == 1U) return (a.y + b.y + c.y) / 3.0;
            return (a.z + b.z + c.z) / 3.0;
        };
        std::nth_element(order_.begin() + begin, order_.begin() + middle, order_.begin() + end,
            [&](const std::uint32_t a, const std::uint32_t b) { return coordinate(a) < coordinate(b); });
        const std::uint32_t left = build(begin, middle);
        const std::uint32_t right = build(middle, end);
        nodes_[node_index] = BvhNode{.bounds = bounds, .left = left, .right = right};
        return node_index;
    }

    const std::vector<SourceVertex>& vertices_;
    const std::vector<Triangle>& triangles_;
    std::vector<std::uint32_t> order_;
    std::vector<BvhNode> nodes_;
};

void interpolate(
    const std::vector<SpatialPrimitiveInput>& source,
    const std::vector<SourceVertex>& vertices,
    const Triangle& triangle,
    const std::array<double, 3>& barycentric,
    const SpatialSkinTransferOptions& options,
    std::vector<WeightedJoint>& output
) {
    output.clear();
    for (std::uint32_t corner = 0U; corner < 3U; ++corner) {
        const SourceVertex& vertex = vertices[triangle.vertices[corner]];
        const PrimitiveBuffers& primitive = *source[vertex.primitive].buffers;
        for (const SkinInfluenceSet& set : primitive.influence_sets) {
            for (std::uint32_t lane = 0U; lane < set.joints.view.component_count; ++lane) {
                const auto joint = read_uint(set.joints.view, vertex.local_vertex, lane);
                const auto weight = read_float(set.weights.view, vertex.local_vertex, lane);
                if (joint && weight && *weight > 0.0) {
                    const double contribution = barycentric[corner] * *weight;
                    const auto existing = std::ranges::find_if(output, [&](const WeightedJoint& value) {
                        return value.joint == *joint;
                    });
                    if (existing == output.end()) output.push_back({*joint, contribution});
                    else existing->weight += contribution;
                }
            }
        }
    }
    std::erase_if(output, [&](const WeightedJoint& value) {
        return !std::isfinite(value.weight) || value.weight < options.minimum_weight;
    });
    std::ranges::sort(output, [](const WeightedJoint& a, const WeightedJoint& b) {
        return a.weight != b.weight ? a.weight > b.weight : a.joint < b.joint;
    });
    if (output.size() > options.maximum_influences) output.resize(options.maximum_influences);
    const double total = std::accumulate(output.begin(), output.end(), 0.0,
        [](const double sum, const WeightedJoint& influence) { return sum + influence.weight; });
    if (total > 0.0) {
        for (WeightedJoint& influence : output) influence.weight /= total;
    }
}

template <typename T>
ImmutableBytes immutable_storage(std::vector<T>&& values) {
    auto bytes = std::make_shared<std::vector<std::byte>>(values.size() * sizeof(T));
    if (!values.empty()) std::memcpy(bytes->data(), values.data(), bytes->size());
    return bytes;
}

}  // namespace

bool SpatialSkinTransferResult::success() const noexcept {
    return report.has_value() && std::ranges::none_of(diagnostics, [](const Diagnostic& value) {
        return value.severity == DiagnosticSeverity::error;
    });
}

SpatialSkinTransferResult transfer_skin_weights_spatially(
    const std::span<const SpatialPrimitiveInput> source_span,
    const std::span<const SpatialPrimitiveInput> target_span,
    const SpatialSkinTransferOptions& options
) {
    SpatialSkinTransferResult result;
    if (source_span.empty() || target_span.empty()) {
        add_error(result, "SKIN_TRANSFER_INPUT_EMPTY", "Source and target require at least one primitive.");
        return result;
    }
    if (options.maximum_influences == 0U || options.maximum_influences > 64U
        || !std::isfinite(options.minimum_weight) || options.minimum_weight < 0.0
        || (options.maximum_distance_m && (!std::isfinite(*options.maximum_distance_m)
            || *options.maximum_distance_m <= 0.0))) {
        add_error(result, "SKIN_TRANSFER_OPTIONS", "Skin transfer options are outside supported finite ranges.");
        return result;
    }
    const std::vector<SpatialPrimitiveInput> source{source_span.begin(), source_span.end()};
    std::vector<SourceVertex> source_vertices;
    std::vector<Triangle> triangles;
    for (std::size_t primitive_index = 0U; primitive_index < source.size(); ++primitive_index) {
        if (!source[primitive_index].buffers
            || !validate_primitive_buffers(*source[primitive_index].buffers).valid()
            || source[primitive_index].buffers->influence_sets.empty()) {
            add_error(result, "SKIN_TRANSFER_SOURCE_INVALID", "Every source primitive must provide valid positions, triangles and skin influences.");
            return result;
        }
        const PrimitiveBuffers& primitive = *source[primitive_index].buffers;
        if (source_vertices.size() + primitive.positions.view.element_count
            > std::numeric_limits<std::uint32_t>::max()) {
            add_error(result, "SKIN_TRANSFER_VERTEX_CAPACITY", "Source vertex count exceeds uint32 spatial-index capacity.");
            return result;
        }
        const std::uint32_t base = static_cast<std::uint32_t>(source_vertices.size());
        for (std::uint64_t vertex = 0U; vertex < primitive.positions.view.element_count; ++vertex) {
            const auto position = read_position(primitive.positions, vertex, source[primitive_index].local_to_world);
            if (!position) {
                add_error(result, "SKIN_TRANSFER_POSITION_INVALID", "A source position is unreadable or non-finite.");
                return result;
            }
            source_vertices.push_back({*position, static_cast<std::uint32_t>(primitive_index), vertex});
        }
        const std::uint64_t index_count = primitive.indices
            ? primitive.indices->view.element_count : primitive.positions.view.element_count;
        if (index_count % 3U != 0U) {
            add_error(result, "SKIN_TRANSFER_TRIANGLES_INVALID", "Source indices must contain triangles.");
            return result;
        }
        for (std::uint64_t offset = 0U; offset < index_count; offset += 3U) {
            Triangle triangle;
            for (std::uint32_t lane = 0U; lane < 3U; ++lane) {
                const auto local = primitive.indices
                    ? read_uint(primitive.indices->view, offset + lane, 0U)
                    : std::optional<std::uint32_t>{static_cast<std::uint32_t>(offset + lane)};
                if (!local || *local >= primitive.positions.view.element_count) {
                    add_error(result, "SKIN_TRANSFER_INDEX_INVALID", "A source triangle index is outside POSITION bounds.");
                    return result;
                }
                triangle.vertices[lane] = base + *local;
            }
            triangles.push_back(triangle);
        }
    }
    if (triangles.empty()) {
        add_error(result, "SKIN_TRANSFER_TRIANGLES_EMPTY", "Source contains no triangles.");
        return result;
    }

    TriangleBvh bvh{source_vertices, triangles};
    SpatialSkinTransferReport report{.source_triangle_count = triangles.size()};
    double distance_sum{};
    result.primitives.reserve(target_span.size());
    for (const SpatialPrimitiveInput& target : target_span) {
        if (!target.buffers || !validate_primitive_buffers(*target.buffers).valid()) {
            add_error(result, "SKIN_TRANSFER_TARGET_INVALID", "Every target primitive must provide valid positions and topology.");
            return result;
        }
        const std::uint64_t vertex_count = target.buffers->positions.view.element_count;
        if (vertex_count > std::numeric_limits<std::size_t>::max() / 4U) {
            add_error(result, "SKIN_TRANSFER_TARGET_TOO_LARGE", "Target vertex buffers exceed addressable memory.");
            return result;
        }
        report.target_vertex_count += vertex_count;
        const std::size_t set_count = (options.maximum_influences + 3U) / 4U;
        std::vector<std::vector<std::uint32_t>> joints(set_count,
            std::vector<std::uint32_t>(static_cast<std::size_t>(vertex_count) * 4U));
        std::vector<std::vector<float>> weights(set_count,
            std::vector<float>(static_cast<std::size_t>(vertex_count) * 4U));
        std::uint32_t observed_max{};
        std::vector<WeightedJoint> influences;
        influences.reserve(64U);
        for (std::uint64_t vertex = 0U; vertex < vertex_count; ++vertex) {
            const auto point = read_position(target.buffers->positions, vertex, target.local_to_world);
            if (!point) {
                add_error(result, "SKIN_TRANSFER_TARGET_POSITION_INVALID", "A target position is unreadable or non-finite.");
                return result;
            }
            const auto [triangle_index, closest] = bvh.nearest(*point);
            const double distance = std::sqrt(closest.distance_squared);
            report.maximum_distance_m = std::max(report.maximum_distance_m, distance);
            if (options.maximum_distance_m && distance > *options.maximum_distance_m) {
                ++report.rejected_vertex_count;
                continue;
            }
            interpolate(source, source_vertices, triangles[triangle_index], closest.barycentric, options, influences);
            if (influences.empty()) {
                ++report.rejected_vertex_count;
                continue;
            }
            ++report.matched_vertex_count;
            distance_sum += distance;
            observed_max = std::max(observed_max, static_cast<std::uint32_t>(influences.size()));
            for (std::size_t influence = 0U; influence < influences.size(); ++influence) {
                const std::size_t set = influence / 4U;
                const std::size_t lane = influence % 4U;
                const std::size_t index = static_cast<std::size_t>(vertex) * 4U + lane;
                joints[set][index] = influences[influence].joint;
                weights[set][index] = static_cast<float>(influences[influence].weight);
            }
            if (options.quality == SpatialTransferQuality::diagnostic
                && report.diagnostic_samples.size() < 1024U) {
                report.diagnostic_samples.push_back({vertex, triangle_index, closest.barycentric, distance});
            }
        }
        TransferredPrimitiveSkin output{
            .max_influences = observed_max,
            .vertex_count = vertex_count,
        };
        const std::size_t used_sets = observed_max == 0U ? 0U : (observed_max + 3U) / 4U;
        output.influence_sets.reserve(used_sets);
        for (std::size_t set = 0U; set < used_sets; ++set) {
            output.influence_sets.push_back({
                .joints = {VertexSemantic::joints, {immutable_storage(std::move(joints[set])), 0U, 0U, vertex_count, 4U, ScalarType::uint32}},
                .weights = {VertexSemantic::weights, {immutable_storage(std::move(weights[set])), 0U, 0U, vertex_count, 4U, ScalarType::float32}},
            });
        }
        report.output_max_influences = std::max(report.output_max_influences, observed_max);
        result.primitives.push_back(std::move(output));
    }
    report.mean_distance_m = report.matched_vertex_count == 0U
        ? 0.0 : distance_sum / static_cast<double>(report.matched_vertex_count);
    if (report.matched_vertex_count == 0U) {
        add_error(result, "SKIN_TRANSFER_NO_MATCH", "No target vertex satisfied the spatial distance policy.");
        return result;
    }
    result.report = std::move(report);
    return result;
}

}  // namespace unified3d::geometry
