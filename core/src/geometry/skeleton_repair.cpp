#include <unified3d/core/geometry/skeleton_repair.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace unified3d::geometry {
namespace {

void add_diagnostic(
    SkeletonRepairResult& result,
    const DiagnosticSeverity severity,
    std::string code,
    std::string message,
    std::string path
) {
    result.diagnostics.push_back({
        .severity = severity,
        .code = std::move(code),
        .message = std::move(message),
        .path = std::move(path),
    });
}

bool matrix_is_finite(const BindMatrix4d& matrix) {
    return std::ranges::all_of(matrix, [](const double value) {
        return std::isfinite(value);
    });
}

bool has_errors(const SkeletonRepairResult& result) {
    return std::ranges::any_of(result.diagnostics, [](const Diagnostic& diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::error;
    });
}

std::size_t element_stride(const BufferView& view) {
    const std::size_t packed = scalar_size(view.scalar_type)
        * static_cast<std::size_t>(view.component_count);
    return view.byte_stride == 0U ? packed : view.byte_stride;
}

template <typename T>
T read_component(
    const BufferView& view,
    const std::uint64_t element,
    const std::uint32_t component
) {
    T value{};
    const std::size_t offset = view.byte_offset
        + static_cast<std::size_t>(element) * element_stride(view)
        + static_cast<std::size_t>(component) * scalar_size(view.scalar_type);
    std::memcpy(&value, view.storage->data() + offset, sizeof(T));
    return value;
}

std::uint32_t read_joint(
    const BufferView& view,
    const std::uint64_t element,
    const std::uint32_t component
) {
    if (view.scalar_type == ScalarType::uint16) {
        return read_component<std::uint16_t>(view, element, component);
    }
    return read_component<std::uint32_t>(view, element, component);
}

double read_weight(
    const BufferView& view,
    const std::uint64_t element,
    const std::uint32_t component
) {
    if (view.scalar_type == ScalarType::float32) {
        return static_cast<double>(read_component<float>(view, element, component));
    }
    return read_component<double>(view, element, component);
}

template <typename T>
ImmutableBytes immutable_bytes(const std::vector<T>& values) {
    auto storage = std::make_shared<std::vector<std::byte>>(values.size() * sizeof(T));
    if (!values.empty()) {
        std::memcpy(storage->data(), values.data(), storage->size());
    }
    return storage;
}

void append_validation(
    SkeletonRepairResult& result,
    ValidationResult validation,
    const std::string& prefix
) {
    for (auto& diagnostic : validation.diagnostics) {
        diagnostic.path = prefix + diagnostic.path;
        result.diagnostics.push_back(std::move(diagnostic));
    }
}

bool validate_skeleton(
    SkeletonRepairResult& result,
    const SkeletonDefinition& skeleton
) {
    if (skeleton.joints.empty()) {
        add_diagnostic(
            result,
            DiagnosticSeverity::error,
            "SKELETON_EMPTY",
            "A skeleton repair requires at least one source joint.",
            "$.skeleton.joints"
        );
        return false;
    }

    std::unordered_set<std::string> names;
    for (std::size_t index = 0U; index < skeleton.joints.size(); ++index) {
        const auto& joint = skeleton.joints[index];
        const std::string path = "$.skeleton.joints[" + std::to_string(index) + "]";
        if (joint.name.empty() || !names.insert(joint.name).second) {
            add_diagnostic(
                result,
                DiagnosticSeverity::error,
                "SKELETON_JOINT_NAME",
                "Joint names must be non-empty and unique.",
                path + ".name"
            );
        }
        if (joint.parent_index.has_value()
            && (*joint.parent_index >= skeleton.joints.size()
                || *joint.parent_index == index)) {
            add_diagnostic(
                result,
                DiagnosticSeverity::error,
                "SKELETON_PARENT",
                "A parent index must reference another source joint.",
                path + ".parent_index"
            );
        }
        if (!matrix_is_finite(joint.local_bind_transform)
            || !matrix_is_finite(joint.inverse_bind_matrix)) {
            add_diagnostic(
                result,
                DiagnosticSeverity::error,
                "SKELETON_BIND_MATRIX",
                "Bind matrices must contain only finite values.",
                path
            );
        }
    }

    for (std::size_t start = 0U; start < skeleton.joints.size(); ++start) {
        std::vector<bool> visited(skeleton.joints.size(), false);
        std::optional<std::uint32_t> cursor = static_cast<std::uint32_t>(start);
        while (cursor.has_value()) {
            if (visited[*cursor]) {
                add_diagnostic(
                    result,
                    DiagnosticSeverity::error,
                    "SKELETON_CYCLE",
                    "The source skeleton hierarchy contains a cycle.",
                    "$.skeleton.joints[" + std::to_string(start) + "]"
                );
                break;
            }
            visited[*cursor] = true;
            cursor = skeleton.joints[*cursor].parent_index;
        }
    }
    return !has_errors(result);
}

}  // namespace

bool SkeletonRepairResult::success() const noexcept {
    return skeleton.has_value() && !has_errors(*this);
}

SkeletonRepairResult repair_skeleton_and_skin(
    const SkeletonDefinition& source_skeleton,
    const std::span<const PrimitiveBuffers> source_primitives,
    const SkeletonRepairOptions& options
) {
    SkeletonRepairResult result;
    const std::size_t joint_count = source_skeleton.joints.size();

    validate_skeleton(result, source_skeleton);
    if (options.maximum_influences == 0U || options.maximum_influences > 32U) {
        add_diagnostic(
            result,
            DiagnosticSeverity::error,
            "SKELETON_MAX_INFLUENCES",
            "maximum_influences must be in the range 1..32.",
            "$.options.maximum_influences"
        );
    }
    if (!std::isfinite(options.minimum_weight) || options.minimum_weight < 0.0) {
        add_diagnostic(
            result,
            DiagnosticSeverity::error,
            "SKELETON_MINIMUM_WEIGHT",
            "minimum_weight must be finite and non-negative.",
            "$.options.minimum_weight"
        );
    }

    std::vector<bool> collapsed(joint_count, false);
    std::vector<std::optional<std::uint32_t>> explicit_targets(joint_count);
    for (std::size_t index = 0U; index < options.collapsed_joints.size(); ++index) {
        const auto& directive = options.collapsed_joints[index];
        const std::string path = "$.options.collapsed_joints[" + std::to_string(index) + "]";
        if (directive.joint_index >= joint_count) {
            add_diagnostic(result, DiagnosticSeverity::error, "SKELETON_COLLAPSE_INDEX", "Collapsed joint index is out of range.", path + ".joint_index");
            continue;
        }
        if (collapsed[directive.joint_index]) {
            add_diagnostic(result, DiagnosticSeverity::error, "SKELETON_COLLAPSE_DUPLICATE", "A joint can be collapsed only once.", path + ".joint_index");
            continue;
        }
        if (directive.target_joint_index.has_value()
            && *directive.target_joint_index >= joint_count) {
            add_diagnostic(result, DiagnosticSeverity::error, "SKELETON_COLLAPSE_TARGET", "Collapse target joint index is out of range.", path + ".target_joint_index");
            continue;
        }
        collapsed[directive.joint_index] = true;
        explicit_targets[directive.joint_index] = directive.target_joint_index;
    }

    std::unordered_map<std::uint32_t, JointRelocation> relocations;
    for (std::size_t index = 0U; index < options.relocated_joints.size(); ++index) {
        const auto& directive = options.relocated_joints[index];
        const std::string path = "$.options.relocated_joints[" + std::to_string(index) + "]";
        if (directive.joint_index >= joint_count) {
            add_diagnostic(result, DiagnosticSeverity::error, "SKELETON_RELOCATE_INDEX", "Relocated joint index is out of range.", path + ".joint_index");
            continue;
        }
        if (collapsed[directive.joint_index]) {
            add_diagnostic(result, DiagnosticSeverity::error, "SKELETON_RELOCATE_COLLAPSED", "A collapsed joint cannot also be relocated.", path + ".joint_index");
            continue;
        }
        if (!matrix_is_finite(directive.corrected_local_bind_transform)
            || !matrix_is_finite(directive.corrected_inverse_bind_matrix)) {
            add_diagnostic(result, DiagnosticSeverity::error, "SKELETON_RELOCATE_MATRIX", "Corrected bind matrices must contain only finite values.", path);
            continue;
        }
        if (!relocations.emplace(directive.joint_index, directive).second) {
            add_diagnostic(result, DiagnosticSeverity::error, "SKELETON_RELOCATE_DUPLICATE", "A joint can be relocated only once.", path + ".joint_index");
        }
    }

    if (std::ranges::all_of(collapsed, [](const bool value) { return value; })) {
        add_diagnostic(result, DiagnosticSeverity::error, "SKELETON_ALL_COLLAPSED", "At least one joint must remain in the repaired skeleton.", "$.options.collapsed_joints");
    }
    if (options.fallback_joint_index.has_value()
        && *options.fallback_joint_index >= joint_count) {
        add_diagnostic(result, DiagnosticSeverity::error, "SKELETON_FALLBACK_INDEX", "Fallback joint index is out of range.", "$.options.fallback_joint_index");
    }

    for (std::size_t index = 0U; index < joint_count; ++index) {
        if (explicit_targets[index].has_value() && collapsed[*explicit_targets[index]]) {
            add_diagnostic(
                result,
                DiagnosticSeverity::error,
                "SKELETON_COLLAPSE_TARGET_REMOVED",
                "An explicit collapse target must be retained.",
                "$.options.collapsed_joints"
            );
        }
    }

    for (std::size_t index = 0U; index < source_primitives.size(); ++index) {
        append_validation(
            result,
            validate_primitive_buffers(source_primitives[index]),
            "$.primitives[" + std::to_string(index) + "]"
        );
    }
    if (has_errors(result)) {
        return result;
    }

    result.source_to_repaired_joint.resize(joint_count);
    std::vector<std::optional<std::uint32_t>> retained_indices(joint_count);
    SkeletonDefinition repaired;
    for (std::size_t old_index = 0U; old_index < joint_count; ++old_index) {
        if (!collapsed[old_index]) {
            retained_indices[old_index] = static_cast<std::uint32_t>(repaired.joints.size());
            result.source_to_repaired_joint[old_index] = retained_indices[old_index];
            repaired.joints.push_back(source_skeleton.joints[old_index]);
        }
    }

    const auto resolve_retained_source = [&](const std::uint32_t source_index) {
        std::optional<std::uint32_t> cursor = source_index;
        if (explicit_targets[source_index].has_value()) {
            cursor = explicit_targets[source_index];
        } else if (collapsed[source_index]) {
            cursor = source_skeleton.joints[source_index].parent_index;
        }
        while (cursor.has_value() && collapsed[*cursor]) {
            cursor = source_skeleton.joints[*cursor].parent_index;
        }
        return cursor;
    };

    for (std::size_t old_index = 0U; old_index < joint_count; ++old_index) {
        if (collapsed[old_index]) {
            const auto target = resolve_retained_source(static_cast<std::uint32_t>(old_index));
            if (!target.has_value()) {
                add_diagnostic(
                    result,
                    DiagnosticSeverity::error,
                    "SKELETON_COLLAPSE_NO_TARGET",
                    "A collapsed root branch requires an explicit retained target.",
                    "$.options.collapsed_joints"
                );
            } else {
                result.source_to_repaired_joint[old_index] = retained_indices[*target];
            }
            continue;
        }

        SkeletonJoint& output_joint = repaired.joints[*retained_indices[old_index]];
        auto parent = source_skeleton.joints[old_index].parent_index;
        while (parent.has_value() && collapsed[*parent]) {
            parent = source_skeleton.joints[*parent].parent_index;
        }
        output_joint.parent_index = parent.has_value() ? retained_indices[*parent] : std::nullopt;
        if (const auto found = relocations.find(static_cast<std::uint32_t>(old_index));
            found != relocations.end()) {
            output_joint.local_bind_transform = found->second.corrected_local_bind_transform;
            output_joint.inverse_bind_matrix = found->second.corrected_inverse_bind_matrix;
        }
    }
    if (has_errors(result)) {
        return result;
    }

    std::optional<std::uint32_t> fallback;
    if (options.fallback_joint_index.has_value()) {
        fallback = result.source_to_repaired_joint[*options.fallback_joint_index];
    }

    result.primitives.reserve(source_primitives.size());
    for (std::size_t primitive_index = 0U;
         primitive_index < source_primitives.size();
         ++primitive_index) {
        const PrimitiveBuffers& source = source_primitives[primitive_index];
        if (source.influence_sets.empty()) {
            result.primitives.push_back(source);
            continue;
        }

        const std::uint64_t vertex_count = source.positions.view.element_count;
        const std::size_t capacity = static_cast<std::size_t>(vertex_count)
            * static_cast<std::size_t>(options.maximum_influences);
        std::vector<std::uint32_t> flattened_joints(capacity, 0U);
        std::vector<float> flattened_weights(capacity, 0.0F);
        std::uint32_t actual_maximum = 0U;

        for (std::uint64_t vertex = 0U; vertex < vertex_count; ++vertex) {
            std::vector<std::pair<std::uint32_t, double>> influences;
            for (const auto& set : source.influence_sets) {
                for (std::uint32_t component = 0U; component < 4U; ++component) {
                    const double weight = read_weight(set.weights.view, vertex, component);
                    if (!std::isfinite(weight) || weight < 0.0) {
                        add_diagnostic(result, DiagnosticSeverity::error, "SKELETON_WEIGHT_VALUE", "Skin weights must be finite and non-negative.", "$.primitives[" + std::to_string(primitive_index) + "].influence_sets");
                        continue;
                    }
                    if (weight <= options.minimum_weight) {
                        continue;
                    }
                    const std::uint32_t source_joint = read_joint(set.joints.view, vertex, component);
                    if (source_joint >= joint_count) {
                        add_diagnostic(result, DiagnosticSeverity::error, "SKELETON_WEIGHT_JOINT", "A skin influence references a joint outside the source skeleton.", "$.primitives[" + std::to_string(primitive_index) + "].influence_sets");
                        continue;
                    }
                    const auto repaired_joint = result.source_to_repaired_joint[source_joint];
                    if (!repaired_joint.has_value()) {
                        add_diagnostic(result, DiagnosticSeverity::error, "SKELETON_WEIGHT_TARGET", "A skin influence could not be mapped to a retained joint.", "$.primitives[" + std::to_string(primitive_index) + "].influence_sets");
                        continue;
                    }
                    if (collapsed[source_joint]) {
                        ++result.remapped_influence_count;
                    }
                    const auto existing = std::ranges::find_if(
                        influences,
                        [&](const auto& influence) {
                            return influence.first == *repaired_joint;
                        }
                    );
                    if (existing == influences.end()) {
                        influences.emplace_back(*repaired_joint, weight);
                    } else {
                        existing->second += weight;
                        ++result.merged_influence_count;
                    }
                }
            }
            if (has_errors(result)) {
                continue;
            }

            std::erase_if(influences, [&](const auto& influence) {
                return influence.second <= options.minimum_weight;
            });
            std::ranges::sort(influences, [](const auto& left, const auto& right) {
                return left.second == right.second
                    ? left.first < right.first
                    : left.second > right.second;
            });
            if (influences.size() > options.maximum_influences) {
                influences.resize(options.maximum_influences);
            }
            if (influences.empty()) {
                ++result.zero_weight_vertex_count;
                if (!fallback.has_value()) {
                    add_diagnostic(result, DiagnosticSeverity::error, "SKELETON_ZERO_WEIGHT_VERTEX", "A vertex lost all influences and no fallback joint was configured.", "$.primitives[" + std::to_string(primitive_index) + "]");
                    continue;
                }
                influences.emplace_back(*fallback, 1.0);
            }

            double weight_sum = 0.0;
            for (const auto& influence : influences) {
                weight_sum += influence.second;
            }
            if (!std::isfinite(weight_sum) || weight_sum <= 0.0) {
                add_diagnostic(result, DiagnosticSeverity::error, "SKELETON_WEIGHT_NORMALIZATION", "Skin weights could not be normalized.", "$.primitives[" + std::to_string(primitive_index) + "]");
                continue;
            }
            actual_maximum = std::max(
                actual_maximum,
                static_cast<std::uint32_t>(influences.size())
            );
            const std::size_t base = static_cast<std::size_t>(vertex)
                * static_cast<std::size_t>(options.maximum_influences);
            for (std::size_t index = 0U; index < influences.size(); ++index) {
                flattened_joints[base + index] = influences[index].first;
                flattened_weights[base + index] = static_cast<float>(
                    influences[index].second / weight_sum
                );
            }
        }
        if (has_errors(result)) {
            continue;
        }

        PrimitiveBuffers output = source;
        output.influence_sets.clear();
        output.max_influences = actual_maximum;
        const std::uint32_t set_count = (actual_maximum + 3U) / 4U;
        for (std::uint32_t set_index = 0U; set_index < set_count; ++set_index) {
            std::vector<std::uint32_t> joints(static_cast<std::size_t>(vertex_count) * 4U, 0U);
            std::vector<float> weights(static_cast<std::size_t>(vertex_count) * 4U, 0.0F);
            for (std::uint64_t vertex = 0U; vertex < vertex_count; ++vertex) {
                for (std::uint32_t component = 0U; component < 4U; ++component) {
                    const std::uint32_t influence_index = set_index * 4U + component;
                    if (influence_index >= options.maximum_influences) {
                        continue;
                    }
                    const std::size_t source_offset = static_cast<std::size_t>(vertex)
                        * static_cast<std::size_t>(options.maximum_influences)
                        + influence_index;
                    const std::size_t destination_offset = static_cast<std::size_t>(vertex) * 4U + component;
                    joints[destination_offset] = flattened_joints[source_offset];
                    weights[destination_offset] = flattened_weights[source_offset];
                }
            }
            output.influence_sets.push_back({
                .joints = {
                    VertexSemantic::joints,
                    {immutable_bytes(joints), 0U, 0U, vertex_count, 4U, ScalarType::uint32},
                },
                .weights = {
                    VertexSemantic::weights,
                    {immutable_bytes(weights), 0U, 0U, vertex_count, 4U, ScalarType::float32},
                },
            });
        }
        result.primitives.push_back(std::move(output));
    }
    if (has_errors(result)) {
        result.primitives.clear();
        return result;
    }

    result.collapsed_joint_count = options.collapsed_joints.size();
    result.relocated_joint_count = options.relocated_joints.size();
    result.requires_animation_retargeting =
        result.collapsed_joint_count != 0U || result.relocated_joint_count != 0U;
    if (result.collapsed_joint_count != 0U) {
        add_diagnostic(
            result,
            DiagnosticSeverity::info,
            "SKELETON_JOINTS_COLLAPSED",
            "Collapsed joint weights were transferred to retained joints and normalized.",
            "$.skeleton"
        );
    }
    if (result.relocated_joint_count != 0U) {
        add_diagnostic(
            result,
            DiagnosticSeverity::warning,
            "SKELETON_ANIMATION_RETARGET_REQUIRED",
            "Relocated bind joints require matching animation-channel retargeting before export.",
            "$.skeleton"
        );
    }
    result.skeleton = std::move(repaired);
    return result;
}

}  // namespace unified3d::geometry
