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
