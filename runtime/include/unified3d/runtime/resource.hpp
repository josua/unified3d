#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <unified3d/adapters/asset_format_adapter.hpp>
#include <unified3d/core/analysis/analysis_record.hpp>
#include <unified3d/core/geometry/buffers.hpp>
#include <unified3d/core/geometry/canonical_fingerprint.hpp>
#include <unified3d/core/geometry/spatial_skin_transfer.hpp>

namespace unified3d::runtime {

struct HandleIdentity {
    std::string session;
    std::uint64_t generation{};
    std::uint64_t object_id{};

    [[nodiscard]] bool operator==(const HandleIdentity&) const = default;
};

struct AssetHandle {
    HandleIdentity identity;

    [[nodiscard]] std::string encode() const;
    [[nodiscard]] bool operator==(const AssetHandle&) const = default;
};

struct VertexBufferHandle {
    HandleIdentity identity;

    [[nodiscard]] std::string encode() const;
    [[nodiscard]] bool operator==(const VertexBufferHandle&) const = default;
};

struct IndexBufferHandle {
    HandleIdentity identity;

    [[nodiscard]] std::string encode() const;
    [[nodiscard]] bool operator==(const IndexBufferHandle&) const = default;
};

struct SkinWeightBufferHandle {
    HandleIdentity identity;

    [[nodiscard]] std::string encode() const;
    [[nodiscard]] bool operator==(const SkinWeightBufferHandle&) const = default;
};

using ResourceHandle = std::variant<
    AssetHandle,
    VertexBufferHandle,
    IndexBufferHandle,
    SkinWeightBufferHandle
>;

[[nodiscard]] std::string encode_handle(const ResourceHandle& handle);

struct ProvenanceRecord {
    std::string producer;
    std::string operation_id;
    std::optional<std::string> source_uri;
    std::optional<std::string> source_revision;
    std::vector<ResourceHandle> parents;
};

struct VertexBufferDescriptor {
    VertexBufferHandle handle;
    geometry::VertexSemantic semantic{geometry::VertexSemantic::position};
    geometry::ScalarType scalar_type{geometry::ScalarType::float32};
    std::uint32_t component_count{};
    std::uint64_t element_count{};
    std::uint64_t byte_length{};
    ProvenanceRecord provenance;
};

struct IndexBufferDescriptor {
    IndexBufferHandle handle;
    geometry::ScalarType scalar_type{geometry::ScalarType::uint32};
    std::uint64_t element_count{};
    std::uint64_t byte_length{};
    ProvenanceRecord provenance;
};

struct SkinWeightBufferDescriptor {
    SkinWeightBufferHandle handle;
    std::uint32_t influence_set{};
    std::uint64_t vertex_count{};
    std::uint64_t byte_length{};
    ProvenanceRecord provenance;
};

struct PrimitiveResourceSet {
    std::string name;
    std::uint64_t source_mesh_index{};
    std::uint64_t source_primitive_index{};
    adapters::GeometryDomain domain{adapters::GeometryDomain::render_vertices};
    adapters::Matrix4d local_to_world{};
    std::uint32_t max_influences{};
    VertexBufferDescriptor positions;
    std::optional<IndexBufferDescriptor> indices;
    std::vector<SkinWeightBufferDescriptor> influence_sets;
};

struct AssetResource {
    AssetHandle handle;
    std::filesystem::path canonical_path;
    analysis::AssetFormat format{analysis::AssetFormat::unknown};
    analysis::AssetContainer container{analysis::AssetContainer::unknown};
    std::uint64_t size_bytes{};
    std::uint64_t retain_count{};
    std::string adapter;
    std::string buffer_coordinate_system;
    double buffer_unit_meters{};
    std::vector<std::string> joint_names;
    std::vector<PrimitiveResourceSet> primitives;
    std::optional<geometry::CanonicalGeometryFingerprint> canonical_geometry_fingerprint;
    ProvenanceRecord provenance;
};

struct RegistryError {
    std::string code;
    std::string message;
};

struct LoadAssetResult {
    std::optional<AssetResource> asset;
    std::optional<RegistryError> error;
    bool reused{false};

    [[nodiscard]] bool success() const noexcept;
};

struct ReleaseAssetResult {
    bool released{false};
    std::uint64_t remaining_references{};
    std::optional<RegistryError> error;

    [[nodiscard]] bool success() const noexcept;
};

struct RegisterBuffersResult {
    std::optional<AssetResource> asset;
    std::optional<RegistryError> error;

    [[nodiscard]] bool success() const noexcept;
};

struct UpdateAssetProvenanceResult {
    std::optional<AssetResource> asset;
    std::optional<RegistryError> error;

    [[nodiscard]] bool success() const noexcept;
};

struct ResolveBuffersResult {
    std::optional<adapters::DecodedAssetBuffers> buffers;
    std::optional<RegistryError> error;

    [[nodiscard]] bool success() const noexcept;
};

class AssetRegistry final {
public:
    AssetRegistry();
    explicit AssetRegistry(std::string session_id);
    ~AssetRegistry();

    AssetRegistry(const AssetRegistry&) = delete;
    AssetRegistry& operator=(const AssetRegistry&) = delete;
    AssetRegistry(AssetRegistry&&) noexcept;
    AssetRegistry& operator=(AssetRegistry&&) noexcept;

    [[nodiscard]] const std::string& session_id() const noexcept;
    [[nodiscard]] std::size_t live_asset_count() const noexcept;

    [[nodiscard]] LoadAssetResult load(
        const std::filesystem::path& path,
        std::string_view cache_variant = {}
    );
    [[nodiscard]] RegisterBuffersResult register_buffers(
        const AssetHandle& owner,
        adapters::DecodedAssetBuffers decoded
    );
    [[nodiscard]] UpdateAssetProvenanceResult update_provenance(
        const AssetHandle& owner,
        ProvenanceRecord provenance
    );
    [[nodiscard]] ResolveBuffersResult resolve_buffers(const AssetHandle& owner) const;
    [[nodiscard]] RegisterBuffersResult register_transferred_skin(
        const AssetHandle& target,
        const AssetHandle& source,
        std::vector<std::string> joint_names,
        std::vector<geometry::TransferredPrimitiveSkin> primitives,
        bool replace_existing = false
    );
    [[nodiscard]] ReleaseAssetResult release(const AssetHandle& handle);
    [[nodiscard]] std::optional<AssetResource> find(const AssetHandle& handle) const;
    [[nodiscard]] bool contains(const VertexBufferHandle& handle) const;
    [[nodiscard]] bool contains(const IndexBufferHandle& handle) const;
    [[nodiscard]] bool contains(const SkinWeightBufferHandle& handle) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] std::string_view asset_format_name(analysis::AssetFormat format) noexcept;
[[nodiscard]] std::string_view asset_container_name(analysis::AssetContainer container) noexcept;

}  // namespace unified3d::runtime
