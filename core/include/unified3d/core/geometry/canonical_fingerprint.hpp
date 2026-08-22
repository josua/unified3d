#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <unified3d/core/diagnostic.hpp>
#include <unified3d/core/geometry/buffers.hpp>

namespace unified3d::geometry {

struct CanonicalGeometryFingerprint {
    std::string algorithm{"triangle-position-soup-fnv1a64-v1"};
    std::string digest;
    std::uint64_t triangle_count{};
    double position_tolerance_m{1.0e-5};
};

struct CanonicalFingerprintResult {
    std::optional<CanonicalGeometryFingerprint> fingerprint;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool success() const noexcept;
};

// Produces an order-, winding-, and seam-independent triangle-soup fingerprint.
// Positions must already use the canonical right-handed Y-up metric domain.
[[nodiscard]] CanonicalFingerprintResult fingerprint_triangle_position_soup(
    std::span<const PrimitiveBuffers> primitives,
    double position_tolerance_m = 1.0e-5
);

}  // namespace unified3d::geometry
