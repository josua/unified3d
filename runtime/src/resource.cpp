#include <unified3d/runtime/resource.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <iomanip>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <system_error>
#include <unordered_map>
#include <utility>

namespace unified3d::runtime {
namespace {

std::string make_session_id() {
    std::random_device source;
    std::array<std::uint32_t, 4> words{};
    for (std::uint32_t& word : words) {
        word = source();
    }
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const std::uint32_t word : words) {
        output << std::setw(8) << word;
    }
    return output.str();
}

std::string encode_identity(const std::string_view prefix, const HandleIdentity& identity) {
    return std::string{prefix} + ":" + identity.session + ":"
        + std::to_string(identity.generation) + ":" + std::to_string(identity.object_id);
}

std::string lowercase_extension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](const unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return extension;
}

std::pair<analysis::AssetFormat, analysis::AssetContainer> detect_format(
    const std::filesystem::path& path
) {
    const std::string extension = lowercase_extension(path);
    if (extension == ".fbx") {
        return {analysis::AssetFormat::fbx, analysis::AssetContainer::fbx};
    }
    if (extension == ".glb") {
        return {analysis::AssetFormat::gltf, analysis::AssetContainer::glb};
    }
    if (extension == ".gltf") {
        return {analysis::AssetFormat::gltf, analysis::AssetContainer::gltf};
    }
    return {analysis::AssetFormat::unknown, analysis::AssetContainer::unknown};
}

std::string revision_key(
    const std::filesystem::path& path,
    const std::uint64_t size,
    const std::filesystem::file_time_type modified,
    const std::string_view cache_variant
) {
    return path.generic_string() + "|" + std::to_string(size) + "|"
        + std::to_string(modified.time_since_epoch().count()) + "|"
        + std::string{cache_variant};
}

}  // namespace

struct AssetRegistry::Impl {
    struct Slot {
        std::uint64_t generation{1U};
        std::optional<AssetResource> resource;
        std::string cache_key;
    };

    struct VertexResource {
        VertexBufferHandle handle;
        geometry::VertexAttributeBuffer buffer;
    };

    struct IndexResource {
        IndexBufferHandle handle;
        geometry::IndexBuffer buffer;
    };

    struct SkinResource {
        SkinWeightBufferHandle handle;
        geometry::SkinInfluenceSet buffers;
    };

    template <typename Resource>
    struct ResourceSlot {
        std::uint64_t generation{1U};
        std::optional<Resource> resource;
    };

    explicit Impl(std::string value) : session(std::move(value)) {}

    std::string session;
    mutable std::mutex mutex;
    std::vector<Slot> slots;
    std::vector<std::uint64_t> free_ids;
    std::unordered_map<std::string, std::uint64_t> live_by_revision;
    std::vector<ResourceSlot<VertexResource>> vertex_slots;
    std::vector<ResourceSlot<IndexResource>> index_slots;
    std::vector<ResourceSlot<SkinResource>> skin_slots;
    std::vector<std::uint64_t> free_vertex_ids;
    std::vector<std::uint64_t> free_index_ids;
    std::vector<std::uint64_t> free_skin_ids;
};

std::string AssetHandle::encode() const {
    return encode_identity("asset", identity);
}

std::string VertexBufferHandle::encode() const {
    return encode_identity("vertex-buffer", identity);
}

std::string IndexBufferHandle::encode() const {
    return encode_identity("index-buffer", identity);
}

std::string SkinWeightBufferHandle::encode() const {
    return encode_identity("skin-weight-buffer", identity);
}

std::string encode_handle(const ResourceHandle& handle) {
    return std::visit([](const auto& current) { return current.encode(); }, handle);
}

bool LoadAssetResult::success() const noexcept {
    return asset.has_value() && !error.has_value();
}

bool ReleaseAssetResult::success() const noexcept {
    return !error.has_value();
}

bool RegisterBuffersResult::success() const noexcept {
    return asset.has_value() && !error.has_value();
}

bool UpdateAssetProvenanceResult::success() const noexcept {
    return asset.has_value() && !error.has_value();
}

bool ResolveBuffersResult::success() const noexcept {
    return buffers.has_value() && !error.has_value();
}

AssetRegistry::AssetRegistry() : AssetRegistry(make_session_id()) {}

AssetRegistry::AssetRegistry(std::string session_id)
    : impl_(std::make_unique<Impl>(std::move(session_id))) {
    if (impl_->session.empty() || impl_->session.find(':') != std::string::npos) {
        throw std::invalid_argument("Runtime session id must be non-empty and contain no colon.");
    }
}

AssetRegistry::~AssetRegistry() = default;
AssetRegistry::AssetRegistry(AssetRegistry&&) noexcept = default;
AssetRegistry& AssetRegistry::operator=(AssetRegistry&&) noexcept = default;

const std::string& AssetRegistry::session_id() const noexcept {
    return impl_->session;
}

std::size_t AssetRegistry::live_asset_count() const noexcept {
    std::scoped_lock lock{impl_->mutex};
    return impl_->live_by_revision.size();
}

LoadAssetResult AssetRegistry::load(
    const std::filesystem::path& path,
    const std::string_view cache_variant
) {
    std::error_code error;
    if (path.empty()) {
        return {
            .asset = std::nullopt,
            .error = RegistryError{"ASSET_PATH_EMPTY", "Asset path must not be empty."},
            .reused = false,
        };
    }
    const std::filesystem::path absolute = std::filesystem::absolute(path, error);
    if (error) {
        return {
            .asset = std::nullopt,
            .error = RegistryError{"ASSET_PATH_INVALID", error.message()},
            .reused = false,
        };
    }
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(absolute, error);
    if (error || !std::filesystem::is_regular_file(canonical, error)) {
        return {
            .asset = std::nullopt,
            .error = RegistryError{
                "ASSET_NOT_REGULAR_FILE",
                "Asset path must identify an existing local regular file.",
            },
            .reused = false,
        };
    }
    const auto [format, container] = detect_format(canonical);
    if (format == analysis::AssetFormat::unknown) {
        return {
            .asset = std::nullopt,
            .error = RegistryError{
                "ASSET_FORMAT_UNSUPPORTED",
                "Initial asset registry accepts only .fbx, .glb, and .gltf files.",
            },
            .reused = false,
        };
    }
    const auto size = static_cast<std::uint64_t>(std::filesystem::file_size(canonical, error));
    if (error) {
        return {
            .asset = std::nullopt,
            .error = RegistryError{"ASSET_STAT_FAILED", error.message()},
            .reused = false,
        };
    }
    const auto modified = std::filesystem::last_write_time(canonical, error);
    if (error) {
        return {
            .asset = std::nullopt,
            .error = RegistryError{"ASSET_STAT_FAILED", error.message()},
            .reused = false,
        };
    }
    const std::string cache_key = revision_key(canonical, size, modified, cache_variant);

    std::scoped_lock lock{impl_->mutex};
    if (const auto existing = impl_->live_by_revision.find(cache_key);
        existing != impl_->live_by_revision.end()) {
        auto& resource = *impl_->slots.at(static_cast<std::size_t>(existing->second - 1U)).resource;
        ++resource.retain_count;
        return {.asset = resource, .error = std::nullopt, .reused = true};
    }

    std::uint64_t object_id{};
    if (impl_->free_ids.empty()) {
        impl_->slots.emplace_back();
        object_id = static_cast<std::uint64_t>(impl_->slots.size());
    } else {
        object_id = impl_->free_ids.back();
        impl_->free_ids.pop_back();
    }
    auto& slot = impl_->slots.at(static_cast<std::size_t>(object_id - 1U));
    AssetResource resource{
        .handle = AssetHandle{{impl_->session, slot.generation, object_id}},
        .canonical_path = canonical,
        .format = format,
        .container = container,
        .size_bytes = size,
        .retain_count = 1U,
        .adapter = {},
        .buffer_coordinate_system = {},
        .buffer_unit_meters = 0.0,
        .joint_names = {},
        .primitives = {},
        .canonical_geometry_fingerprint = std::nullopt,
        .provenance = {
            .producer = "asset.load",
            .operation_id = "load:" + impl_->session + ":" + std::to_string(slot.generation)
                + ":" + std::to_string(object_id),
            .source_uri = canonical.generic_string(),
            .source_revision = cache_key,
            .parents = {},
        },
    };
    slot.cache_key = cache_key;
    slot.resource = resource;
    impl_->live_by_revision.emplace(cache_key, object_id);
    return {.asset = std::move(resource), .error = std::nullopt, .reused = false};
}

RegisterBuffersResult AssetRegistry::register_buffers(
    const AssetHandle& owner,
    adapters::DecodedAssetBuffers decoded
) {
    std::scoped_lock lock{impl_->mutex};
    if (owner.identity.session != impl_->session || owner.identity.object_id == 0U
        || owner.identity.object_id > impl_->slots.size()) {
        return {
            .asset = std::nullopt,
            .error = RegistryError{
                "INVALID_HANDLE",
                "Asset handle is not owned by this Runtime session.",
            },
        };
    }
    auto& asset_slot = impl_->slots[static_cast<std::size_t>(owner.identity.object_id - 1U)];
    if (asset_slot.generation != owner.identity.generation || !asset_slot.resource.has_value()) {
        return {
            .asset = std::nullopt,
            .error = RegistryError{
                "STALE_HANDLE",
                "Asset handle is released, expired, or from an older generation.",
            },
        };
    }
    AssetResource& asset = *asset_slot.resource;
    if (!asset.primitives.empty()) {
        if (asset.adapter == decoded.adapter) {
            return {.asset = asset, .error = std::nullopt};
        }
        return {
            .asset = std::nullopt,
            .error = RegistryError{
                "ASSET_BUFFERS_ALREADY_REGISTERED",
                "An asset revision may be decoded by only one adapter in a Runtime session.",
            },
        };
    }
    if (decoded.adapter.empty() || decoded.primitives.empty()) {
        return {
            .asset = std::nullopt,
            .error = RegistryError{
                "ASSET_BUFFERS_EMPTY",
                "A native adapter must provide its name and at least one primitive.",
            },
        };
    }
    for (const adapters::DecodedPrimitive& primitive : decoded.primitives) {
        const ValidationResult validation = geometry::validate_primitive_buffers(primitive.buffers);
        if (!validation.valid()) {
            return {
                .asset = std::nullopt,
                .error = RegistryError{
                    "ASSET_BUFFERS_INVALID",
                    validation.diagnostics.empty()
                        ? "Native adapter produced invalid geometry buffers."
                        : validation.diagnostics.front().message,
                },
            };
        }
    }

    std::vector<geometry::PrimitiveBuffers> fingerprint_buffers;
    fingerprint_buffers.reserve(decoded.primitives.size());
    for (const adapters::DecodedPrimitive& primitive : decoded.primitives) {
        fingerprint_buffers.push_back(primitive.buffers);
    }
    const geometry::CanonicalFingerprintResult fingerprint =
        geometry::fingerprint_triangle_position_soup(fingerprint_buffers);
    if (!fingerprint.success()) {
        return {
            .asset = std::nullopt,
            .error = RegistryError{
                "ASSET_FINGERPRINT_FAILED",
                fingerprint.diagnostics.empty()
                    ? "Canonical geometry fingerprinting failed."
                    : fingerprint.diagnostics.front().message,
            },
        };
    }

    const auto provenance = [&](const std::string& kind, const std::uint64_t generation,
                                const std::uint64_t object_id) {
        return ProvenanceRecord{
            .producer = decoded.adapter,
            .operation_id = "decode:" + kind + ":" + impl_->session + ":"
                + std::to_string(generation) + ":" + std::to_string(object_id),
            .source_uri = asset.provenance.source_uri,
            .source_revision = asset.provenance.source_revision,
            .parents = {owner},
        };
    };

    asset.adapter = decoded.adapter;
    asset.buffer_coordinate_system = std::move(decoded.coordinate_system);
    asset.buffer_unit_meters = decoded.unit_meters;
    asset.joint_names = std::move(decoded.joint_names);
    asset.canonical_geometry_fingerprint = fingerprint.fingerprint;
    asset.primitives.reserve(decoded.primitives.size());
    for (adapters::DecodedPrimitive& primitive : decoded.primitives) {
        std::uint64_t vertex_id{};
        if (impl_->free_vertex_ids.empty()) {
            impl_->vertex_slots.emplace_back();
            vertex_id = static_cast<std::uint64_t>(impl_->vertex_slots.size());
        } else {
            vertex_id = impl_->free_vertex_ids.back();
            impl_->free_vertex_ids.pop_back();
        }
        auto& vertex_slot = impl_->vertex_slots[static_cast<std::size_t>(vertex_id - 1U)];
        const VertexBufferHandle vertex_handle{{impl_->session, vertex_slot.generation, vertex_id}};
        const auto vertex_provenance = provenance("positions", vertex_slot.generation, vertex_id);
        VertexBufferDescriptor vertex_descriptor{
            .handle = vertex_handle,
            .semantic = primitive.buffers.positions.semantic,
            .scalar_type = primitive.buffers.positions.view.scalar_type,
            .component_count = primitive.buffers.positions.view.component_count,
            .element_count = primitive.buffers.positions.view.element_count,
            .byte_length = primitive.buffers.positions.view.storage->size(),
            .provenance = vertex_provenance,
        };
        vertex_slot.resource = Impl::VertexResource{
            .handle = vertex_handle,
            .buffer = primitive.buffers.positions,
        };

        std::optional<IndexBufferDescriptor> index_descriptor;
        if (primitive.buffers.indices.has_value()) {
            std::uint64_t index_id{};
            if (impl_->free_index_ids.empty()) {
                impl_->index_slots.emplace_back();
                index_id = static_cast<std::uint64_t>(impl_->index_slots.size());
            } else {
                index_id = impl_->free_index_ids.back();
                impl_->free_index_ids.pop_back();
            }
            auto& index_slot = impl_->index_slots[static_cast<std::size_t>(index_id - 1U)];
            const IndexBufferHandle index_handle{{impl_->session, index_slot.generation, index_id}};
            const auto index_provenance = provenance("indices", index_slot.generation, index_id);
            index_descriptor = IndexBufferDescriptor{
                .handle = index_handle,
                .scalar_type = primitive.buffers.indices->view.scalar_type,
                .element_count = primitive.buffers.indices->view.element_count,
                .byte_length = primitive.buffers.indices->view.storage->size(),
                .provenance = index_provenance,
            };
            index_slot.resource = Impl::IndexResource{
                .handle = index_handle,
                .buffer = *primitive.buffers.indices,
            };
        }

        std::vector<SkinWeightBufferDescriptor> skin_descriptors;
        skin_descriptors.reserve(primitive.buffers.influence_sets.size());
        for (std::size_t set = 0U; set < primitive.buffers.influence_sets.size(); ++set) {
            std::uint64_t skin_id{};
            if (impl_->free_skin_ids.empty()) {
                impl_->skin_slots.emplace_back();
                skin_id = static_cast<std::uint64_t>(impl_->skin_slots.size());
            } else {
                skin_id = impl_->free_skin_ids.back();
                impl_->free_skin_ids.pop_back();
            }
            auto& skin_slot = impl_->skin_slots[static_cast<std::size_t>(skin_id - 1U)];
            const SkinWeightBufferHandle skin_handle{{impl_->session, skin_slot.generation, skin_id}};
            const auto skin_provenance = provenance("skin", skin_slot.generation, skin_id);
            const geometry::SkinInfluenceSet& influence = primitive.buffers.influence_sets[set];
            skin_descriptors.push_back(SkinWeightBufferDescriptor{
                .handle = skin_handle,
                .influence_set = static_cast<std::uint32_t>(set),
                .vertex_count = influence.joints.view.element_count,
                .byte_length = influence.joints.view.storage->size()
                    + influence.weights.view.storage->size(),
                .provenance = skin_provenance,
            });
            skin_slot.resource = Impl::SkinResource{
                .handle = skin_handle,
                .buffers = influence,
            };
        }

        asset.primitives.push_back(PrimitiveResourceSet{
            .name = std::move(primitive.name),
            .source_mesh_index = primitive.source_mesh_index,
            .source_primitive_index = primitive.source_primitive_index,
            .domain = primitive.domain,
            .local_to_world = primitive.local_to_world,
            .max_influences = primitive.buffers.max_influences,
            .positions = std::move(vertex_descriptor),
            .indices = std::move(index_descriptor),
            .influence_sets = std::move(skin_descriptors),
        });
    }
    return {.asset = asset, .error = std::nullopt};
}

UpdateAssetProvenanceResult AssetRegistry::update_provenance(
    const AssetHandle& owner,
    ProvenanceRecord provenance
) {
    std::scoped_lock lock{impl_->mutex};
    if (owner.identity.session != impl_->session || owner.identity.object_id == 0U
        || owner.identity.object_id > impl_->slots.size()) {
        return {
            .asset = std::nullopt,
            .error = RegistryError{
                "INVALID_HANDLE",
                "Asset handle is not owned by this Runtime session.",
            },
        };
    }
    auto& slot = impl_->slots[static_cast<std::size_t>(owner.identity.object_id - 1U)];
    if (slot.generation != owner.identity.generation || !slot.resource.has_value()) {
        return {
            .asset = std::nullopt,
            .error = RegistryError{
                "STALE_HANDLE",
                "Asset handle is released, expired, or from an older generation.",
            },
        };
    }
    if (provenance.producer.empty() || provenance.operation_id.empty()) {
        return {
            .asset = std::nullopt,
            .error = RegistryError{
                "PROVENANCE_INVALID",
                "Asset provenance requires a producer and operation id.",
            },
        };
    }
    slot.resource->provenance = std::move(provenance);
    return {.asset = *slot.resource, .error = std::nullopt};
}

ResolveBuffersResult AssetRegistry::resolve_buffers(const AssetHandle& owner) const {
    std::scoped_lock lock{impl_->mutex};
    if (owner.identity.session != impl_->session || owner.identity.object_id == 0U
        || owner.identity.object_id > impl_->slots.size()) {
        return {.error = RegistryError{"INVALID_HANDLE", "Asset handle is not owned by this Runtime session."}};
    }
    const auto& slot = impl_->slots[static_cast<std::size_t>(owner.identity.object_id - 1U)];
    if (slot.generation != owner.identity.generation || !slot.resource) {
        return {.error = RegistryError{"STALE_HANDLE", "Asset handle is released, expired, or from an older generation."}};
    }
    const AssetResource& asset = *slot.resource;
    adapters::DecodedAssetBuffers decoded{
        .adapter = asset.adapter,
        .coordinate_system = asset.buffer_coordinate_system,
        .unit_meters = asset.buffer_unit_meters,
        .joint_names = asset.joint_names,
    };
    decoded.primitives.reserve(asset.primitives.size());
    for (const PrimitiveResourceSet& primitive : asset.primitives) {
        const auto& vertex_slot = impl_->vertex_slots.at(
            static_cast<std::size_t>(primitive.positions.handle.identity.object_id - 1U));
        if (vertex_slot.generation != primitive.positions.handle.identity.generation
            || !vertex_slot.resource) {
            return {.error = RegistryError{"STALE_CHILD_HANDLE", "Asset POSITION resource is stale."}};
        }
        geometry::PrimitiveBuffers buffers{.positions = vertex_slot.resource->buffer};
        if (primitive.indices) {
            const auto& index_slot = impl_->index_slots.at(
                static_cast<std::size_t>(primitive.indices->handle.identity.object_id - 1U));
            if (index_slot.generation != primitive.indices->handle.identity.generation
                || !index_slot.resource) {
                return {.error = RegistryError{"STALE_CHILD_HANDLE", "Asset index resource is stale."}};
            }
            buffers.indices = index_slot.resource->buffer;
        }
        buffers.max_influences = primitive.max_influences;
        buffers.influence_sets.reserve(primitive.influence_sets.size());
        for (const SkinWeightBufferDescriptor& descriptor : primitive.influence_sets) {
            const auto& skin_slot = impl_->skin_slots.at(
                static_cast<std::size_t>(descriptor.handle.identity.object_id - 1U));
            if (skin_slot.generation != descriptor.handle.identity.generation
                || !skin_slot.resource) {
                return {.error = RegistryError{"STALE_CHILD_HANDLE", "Asset skin resource is stale."}};
            }
            buffers.influence_sets.push_back(skin_slot.resource->buffers);
        }
        decoded.primitives.push_back(adapters::DecodedPrimitive{
            .name = primitive.name,
            .source_mesh_index = primitive.source_mesh_index,
            .source_primitive_index = primitive.source_primitive_index,
            .domain = primitive.domain,
            .local_to_world = primitive.local_to_world,
            .buffers = std::move(buffers),
        });
    }
    return {.buffers = std::move(decoded)};
}

RegisterBuffersResult AssetRegistry::register_transferred_skin(
    const AssetHandle& target,
    const AssetHandle& source,
    std::vector<std::string> joint_names,
    std::vector<geometry::TransferredPrimitiveSkin> primitives,
    const bool replace_existing
) {
    std::scoped_lock lock{impl_->mutex};
    const auto resolve_asset = [&](const AssetHandle& handle) -> AssetResource* {
        if (handle.identity.session != impl_->session || handle.identity.object_id == 0U
            || handle.identity.object_id > impl_->slots.size()) return nullptr;
        auto& slot = impl_->slots[static_cast<std::size_t>(handle.identity.object_id - 1U)];
        return slot.generation == handle.identity.generation && slot.resource
            ? &*slot.resource : nullptr;
    };
    AssetResource* target_asset = resolve_asset(target);
    AssetResource* source_asset = resolve_asset(source);
    if (!target_asset || !source_asset) {
        return {.error = RegistryError{"STALE_HANDLE", "Source or target asset handle is stale."}};
    }
    if (primitives.size() != target_asset->primitives.size()) {
        return {.error = RegistryError{"SKIN_PRIMITIVE_COUNT", "Transferred skin primitive count must match the target asset."}};
    }
    if (joint_names.empty()) {
        return {.error = RegistryError{"SKIN_JOINTS_EMPTY", "Transferred skin requires the donor joint table."}};
    }
    for (const PrimitiveResourceSet& primitive : target_asset->primitives) {
        if (!replace_existing && !primitive.influence_sets.empty()) {
            return {.error = RegistryError{"TARGET_ALREADY_SKINNED", "Target already owns skin influence resources."}};
        }
        for (const SkinWeightBufferDescriptor& descriptor : primitive.influence_sets) {
            if (descriptor.handle.identity.object_id == 0U
                || descriptor.handle.identity.object_id > impl_->skin_slots.size()) {
                return {.error = RegistryError{"STALE_CHILD_HANDLE", "Existing target skin resource is invalid."}};
            }
            const auto& slot = impl_->skin_slots[
                static_cast<std::size_t>(descriptor.handle.identity.object_id - 1U)
            ];
            if (slot.generation != descriptor.handle.identity.generation || !slot.resource) {
                return {.error = RegistryError{"STALE_CHILD_HANDLE", "Existing target skin resource is stale."}};
            }
        }
    }
    for (std::size_t primitive_index = 0U; primitive_index < primitives.size(); ++primitive_index) {
        const auto& transferred = primitives[primitive_index];
        const auto vertex_count = target_asset->primitives[primitive_index].positions.element_count;
        if (transferred.vertex_count != vertex_count) {
            return {.error = RegistryError{"SKIN_VERTEX_COUNT", "Transferred skin vertex count must match target POSITION count."}};
        }
        geometry::PrimitiveBuffers validation_buffers{
            .positions = impl_->vertex_slots[
                static_cast<std::size_t>(target_asset->primitives[primitive_index].positions.handle.identity.object_id - 1U)
            ].resource->buffer,
            .influence_sets = transferred.influence_sets,
            .max_influences = transferred.max_influences,
        };
        if (!geometry::validate_primitive_buffers(validation_buffers).valid()) {
            return {.error = RegistryError{"SKIN_BUFFERS_INVALID", "Transferred influence buffers failed the canonical buffer contract."}};
        }
    }
    if (replace_existing) {
        for (PrimitiveResourceSet& primitive : target_asset->primitives) {
            for (const SkinWeightBufferDescriptor& descriptor : primitive.influence_sets) {
                auto& slot = impl_->skin_slots[
                    static_cast<std::size_t>(descriptor.handle.identity.object_id - 1U)
                ];
                slot.resource.reset();
                ++slot.generation;
                impl_->free_skin_ids.push_back(descriptor.handle.identity.object_id);
            }
            primitive.influence_sets.clear();
            primitive.max_influences = 0U;
        }
    }
    target_asset->joint_names = std::move(joint_names);
    for (std::size_t primitive_index = 0U; primitive_index < primitives.size(); ++primitive_index) {
        auto& target_primitive = target_asset->primitives[primitive_index];
        auto& transferred = primitives[primitive_index];
        target_primitive.max_influences = transferred.max_influences;
        for (std::size_t set = 0U; set < transferred.influence_sets.size(); ++set) {
            std::uint64_t skin_id{};
            if (impl_->free_skin_ids.empty()) {
                impl_->skin_slots.emplace_back();
                skin_id = impl_->skin_slots.size();
            } else {
                skin_id = impl_->free_skin_ids.back();
                impl_->free_skin_ids.pop_back();
            }
            auto& skin_slot = impl_->skin_slots[static_cast<std::size_t>(skin_id - 1U)];
            const SkinWeightBufferHandle handle{{impl_->session, skin_slot.generation, skin_id}};
            const auto& buffers = transferred.influence_sets[set];
            const ProvenanceRecord provenance{
                .producer = "skin.transfer",
                .operation_id = "skin-transfer:" + impl_->session + ":"
                    + std::to_string(skin_slot.generation) + ":" + std::to_string(skin_id),
                .source_uri = target_asset->provenance.source_uri,
                .source_revision = target_asset->provenance.source_revision,
                .parents = {source, target_primitive.positions.handle},
            };
            target_primitive.influence_sets.push_back(SkinWeightBufferDescriptor{
                .handle = handle,
                .influence_set = static_cast<std::uint32_t>(set),
                .vertex_count = transferred.vertex_count,
                .byte_length = buffers.joints.view.storage->size() + buffers.weights.view.storage->size(),
                .provenance = provenance,
            });
            skin_slot.resource = Impl::SkinResource{.handle = handle, .buffers = std::move(transferred.influence_sets[set])};
        }
    }
    return {.asset = *target_asset};
}

ReleaseAssetResult AssetRegistry::release(const AssetHandle& handle) {
    std::scoped_lock lock{impl_->mutex};
    if (handle.identity.session != impl_->session || handle.identity.object_id == 0U
        || handle.identity.object_id > impl_->slots.size()) {
        return {.error = RegistryError{"INVALID_HANDLE", "Asset handle is not owned by this Runtime session."}};
    }
    auto& slot = impl_->slots[static_cast<std::size_t>(handle.identity.object_id - 1U)];
    if (slot.generation != handle.identity.generation || !slot.resource.has_value()) {
        return {.error = RegistryError{"STALE_HANDLE", "Asset handle is released, expired, or from an older generation."}};
    }
    if (slot.resource->retain_count > 1U) {
        --slot.resource->retain_count;
        return {
            .released = false,
            .remaining_references = slot.resource->retain_count,
            .error = std::nullopt,
        };
    }

    for (const PrimitiveResourceSet& primitive : slot.resource->primitives) {
        const auto release_vertex = [&](const VertexBufferHandle& child) {
            auto& child_slot = impl_->vertex_slots[
                static_cast<std::size_t>(child.identity.object_id - 1U)
            ];
            child_slot.resource.reset();
            ++child_slot.generation;
            impl_->free_vertex_ids.push_back(child.identity.object_id);
        };
        const auto release_index = [&](const IndexBufferHandle& child) {
            auto& child_slot = impl_->index_slots[
                static_cast<std::size_t>(child.identity.object_id - 1U)
            ];
            child_slot.resource.reset();
            ++child_slot.generation;
            impl_->free_index_ids.push_back(child.identity.object_id);
        };
        const auto release_skin = [&](const SkinWeightBufferHandle& child) {
            auto& child_slot = impl_->skin_slots[
                static_cast<std::size_t>(child.identity.object_id - 1U)
            ];
            child_slot.resource.reset();
            ++child_slot.generation;
            impl_->free_skin_ids.push_back(child.identity.object_id);
        };
        release_vertex(primitive.positions.handle);
        if (primitive.indices.has_value()) {
            release_index(primitive.indices->handle);
        }
        for (const SkinWeightBufferDescriptor& child : primitive.influence_sets) {
            release_skin(child.handle);
        }
    }
    impl_->live_by_revision.erase(slot.cache_key);
    slot.resource.reset();
    slot.cache_key.clear();
    ++slot.generation;
    impl_->free_ids.push_back(handle.identity.object_id);
    return {.released = true, .remaining_references = 0U, .error = std::nullopt};
}

std::optional<AssetResource> AssetRegistry::find(const AssetHandle& handle) const {
    std::scoped_lock lock{impl_->mutex};
    if (handle.identity.session != impl_->session || handle.identity.object_id == 0U
        || handle.identity.object_id > impl_->slots.size()) {
        return std::nullopt;
    }
    const auto& slot = impl_->slots[static_cast<std::size_t>(handle.identity.object_id - 1U)];
    if (slot.generation != handle.identity.generation) {
        return std::nullopt;
    }
    return slot.resource;
}

bool AssetRegistry::contains(const VertexBufferHandle& handle) const {
    std::scoped_lock lock{impl_->mutex};
    if (handle.identity.session != impl_->session || handle.identity.object_id == 0U
        || handle.identity.object_id > impl_->vertex_slots.size()) {
        return false;
    }
    const auto& slot = impl_->vertex_slots[
        static_cast<std::size_t>(handle.identity.object_id - 1U)
    ];
    return slot.generation == handle.identity.generation && slot.resource.has_value();
}

bool AssetRegistry::contains(const IndexBufferHandle& handle) const {
    std::scoped_lock lock{impl_->mutex};
    if (handle.identity.session != impl_->session || handle.identity.object_id == 0U
        || handle.identity.object_id > impl_->index_slots.size()) {
        return false;
    }
    const auto& slot = impl_->index_slots[
        static_cast<std::size_t>(handle.identity.object_id - 1U)
    ];
    return slot.generation == handle.identity.generation && slot.resource.has_value();
}

bool AssetRegistry::contains(const SkinWeightBufferHandle& handle) const {
    std::scoped_lock lock{impl_->mutex};
    if (handle.identity.session != impl_->session || handle.identity.object_id == 0U
        || handle.identity.object_id > impl_->skin_slots.size()) {
        return false;
    }
    const auto& slot = impl_->skin_slots[
        static_cast<std::size_t>(handle.identity.object_id - 1U)
    ];
    return slot.generation == handle.identity.generation && slot.resource.has_value();
}

std::string_view asset_format_name(const analysis::AssetFormat format) noexcept {
    switch (format) {
        case analysis::AssetFormat::fbx:
            return "FBX";
        case analysis::AssetFormat::gltf:
            return "GLTF";
        case analysis::AssetFormat::unknown:
            return "UNKNOWN";
    }
    return "UNKNOWN";
}

std::string_view asset_container_name(const analysis::AssetContainer container) noexcept {
    switch (container) {
        case analysis::AssetContainer::fbx:
            return "FBX";
        case analysis::AssetContainer::glb:
            return "GLB";
        case analysis::AssetContainer::gltf:
            return "GLTF";
        case analysis::AssetContainer::unknown:
            return "UNKNOWN";
    }
    return "UNKNOWN";
}

}  // namespace unified3d::runtime
