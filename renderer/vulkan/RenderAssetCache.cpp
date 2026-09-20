#include "vulkan/RenderAssetCache.hpp"

#include "vulkan/Device.hpp"

#include <algorithm>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace rubia::rhi::vulkan
{
RenderAssetCache::~RenderAssetCache()
{
    reset();
}

bool RenderAssetCache::empty() const noexcept
{
    const auto emptyPrepared = [](const auto& list)
    {
        return std::none_of(list.begin(), list.end(),
                            [](const auto& entry) { return bool(entry.resource); });
    };
    if (!emptyPrepared(shaders_) || !emptyPrepared(programs_) || !emptyPrepared(templates_) ||
        !emptyPrepared(models_))
    {
        return false;
    }
    return std::none_of(textures_.begin(), textures_.end(),
                        [](const auto& entry) { return bool(entry.texture); }) &&
           std::none_of(materials_.begin(), materials_.end(),
                        [](const auto& entry) { return bool(entry.material); }) &&
           std::none_of(meshes_.begin(), meshes_.end(),
                        [](const auto& entry) { return bool(entry.mesh); });
}

void RenderAssetCache::publishTexture(asset::TextureAssetHandle handle, GpuTexture texture)
{
    if (!handle || !texture)
    {
        throw std::invalid_argument("invalid texture publication");
    }
    if (handle.index < textures_.size() && textures_[handle.index].texture)
    {
        throw std::logic_error("initial texture publication cannot replace an existing slot");
    }
    if (handle.index >= textures_.size())
    {
        textures_.resize(static_cast<std::size_t>(handle.index) + 1);
    }
    if (!nextPublication_)
    {
        throw std::overflow_error("texture publication counter exhausted");
    }
    textures_[handle.index].publication = nextPublication_++;
    textures_[handle.index].texture = std::move(texture);
    textures_[handle.index].generation = handle.generation;
}

void RenderAssetCache::publishMesh(asset::MeshAssetHandle handle, Mesh mesh)
{
    if (!handle || !mesh)
    {
        throw std::invalid_argument("invalid mesh publication");
    }
    if (handle.index < meshes_.size() && meshes_[handle.index].mesh)
    {
        throw std::logic_error("initial mesh publication cannot replace an existing slot");
    }
    if (handle.index >= meshes_.size())
    {
        meshes_.resize(static_cast<std::size_t>(handle.index) + 1);
    }
    meshes_[handle.index].mesh = std::move(mesh);
    meshes_[handle.index].generation = handle.generation;
}

GpuTexture RenderAssetCache::commitTextureReplacement(
    const Device& device,
    const asset::AssetManager& assets,
    asset::TextureAssetHandle handle,
    GpuTexture replacement)
{
    if (domain_ && domain_ != assets.domain())
    {
        throw std::invalid_argument("texture replacement uses another asset domain");
    }
    return replaceTexture(device, handle, std::move(replacement));
}

GpuTexture RenderAssetCache::replaceTexture(const Device& device, asset::TextureAssetHandle handle,
                                            GpuTexture replacement)
{
    if (!device || !replacement || tryTexture(handle) == nullptr)
    {
        throw std::invalid_argument(
            "cannot replace a texture absent from RenderAssetCache");
    }

    struct MaterialTextureUpdate
    {
        GpuMaterial* material = nullptr;
        const asset::MaterialTemplateAsset* materialTemplate = nullptr;
        std::vector<const GpuTexture*> textures;
    };
    std::vector<MaterialTextureUpdate> updates;

    for (uint32_t index = 0;
         index < static_cast<uint32_t>(materials_.size());
         ++index)
    {
        MaterialEntry& entry = materials_[index];
        if (!entry.material || entry.generation == 0)
        {
            continue;
        }

        if (std::find(entry.textures.begin(), entry.textures.end(), handle) == entry.textures.end())
        {
            continue;
        }

        MaterialTextureUpdate update{};
        update.material = &entry.material;
        update.materialTemplate = &entry.layout;
        update.textures.reserve(entry.textures.size());
        for (asset::TextureAssetHandle textureHandle : entry.textures)
        {
            update.textures.push_back(
                textureHandle == handle
                    ? &replacement
                    : &texture(textureHandle));
        }
        updates.push_back(std::move(update));
    }

    if (!nextPublication_)
    {
        throw std::overflow_error("texture publication counter exhausted");
    }
    // All references were validated above. vkUpdateDescriptorSets has no
    // failure return; after these writes the no-throw move commits ownership.
    for (MaterialTextureUpdate& update : updates)
    {
        update.material->updateTextures(
            device,
            *update.materialTemplate,
            update.textures);
    }
    GpuTexture previous = std::move(textures_[handle.index].texture);
    textures_[handle.index].texture = std::move(replacement);
    textures_[handle.index].revision = 0;  // Legacy transaction has not committed CPU content yet.
    textures_[handle.index].requested = 0; // Invalidate any older pending versioned publication.
    textures_[handle.index].publication = nextPublication_++;
    return previous;
}

RenderAssetCache::PreparationVersions RenderAssetCache::inspectPreparation(
    const ResourcePreparationTarget& target) const
{
    if (target.cache != this || !target.domain || !target.revision ||
        (domain_ && domain_ != target.domain))
    {
        throw std::invalid_argument("invalid preparation version or asset domain");
    }
    return std::visit(
        [&](auto handle) -> PreparationVersions
        {
            if (!handle)
            {
                throw std::invalid_argument("invalid preparation handle");
            }
            const auto& list = entries(handle);
            if (handle.index >= list.size())
            {
                return {};
            }
            const auto& entry = list[handle.index];
            if (entry.generation && entry.generation != handle.generation)
            {
                throw std::invalid_argument(
                    "cache slot belongs to another asset generation; reset cache first");
            }
            return {entry.revision, entry.requested, entry.residentDependencies,
                    entry.requestedDependencies};
        },
        target.asset);
}
bool RenderAssetCache::dependenciesCurrent(const ResourcePreparationTarget& target) const
{
    for (const auto& dependency : target.dependencies)
    {
        const auto version =
            inspectPreparation({const_cast<RenderAssetCache*>(this), dependency.handle,
                                dependency.revision, target.domain});
        if (version.resident != dependency.revision)
        {
            return false;
        }
    }
    return true;
}
void RenderAssetCache::acceptPreparation(const ResourcePreparationTarget& target)
{
    static_cast<void>(inspectPreparation(target));
    auto dependencies = target.dependencies;
    std::visit(
        [&](auto handle)
        {
            auto& list = entries(handle);
            if (handle.index >= list.size())
            {
                list.resize(static_cast<size_t>(handle.index) + 1);
            }
            auto& entry = list[handle.index];
            entry.generation = handle.generation;
            entry.requested = target.revision;
            entry.requestedDependencies = std::move(dependencies);
        },
        target.asset);
    domain_ = target.domain;
}

void RenderAssetCache::setTextureVersion(asset::AssetDomainId domain,
                                         asset::AssetVersion<asset::TextureAsset> version)
{
    if (!tryTexture(version.handle))
    {
        throw std::invalid_argument("texture is not resident");
    }
    acceptPreparation({this, version.handle, version.contentRevision, std::move(domain)});
    textures_[version.handle.index].revision = version.contentRevision;
}

uint64_t RenderAssetCache::texturePublication(asset::TextureAssetHandle handle) const noexcept
{
    return tryTexture(handle) ? textures_[handle.index].publication : 0;
}

void RenderAssetCache::publishPreparedTexture(const Device& device,
                                              const ResourcePreparationTarget& target,
                                              GpuTexture texture)
{
    const auto version = inspectPreparation(target);
    if (version.requested != target.revision ||
        version.requestedDependencies != target.dependencies)
    {
        throw std::logic_error("texture preparation has been superseded");
    }
    auto dependencies = target.dependencies;
    if (!dependenciesCurrent(target))
    {
        throw std::logic_error("texture dependencies changed");
    }
    const auto handle = std::get<asset::TextureAssetHandle>(target.asset);
    if (tryTexture(handle))
    {
        // Initial implementation commits at a frame boundary with a conservative GPU wait.
        // New uploads never overwrite the image still used by existing frames.
        device.waitIdle();
        auto previous = replaceTexture(device, handle, std::move(texture));
    }
    else
    {
        publishTexture(handle, std::move(texture));
    }
    textures_[handle.index].residentDependencies = std::move(dependencies);
    textures_[handle.index].revision = target.revision;
    textures_[handle.index].requested = target.revision;
}

void RenderAssetCache::publishPreparedMesh(const Device& device,
                                           const ResourcePreparationTarget& target, Mesh mesh)
{
    const auto version = inspectPreparation(target);
    if (!mesh || version.requested != target.revision ||
        version.requestedDependencies != target.dependencies)
    {
        throw std::logic_error("invalid or superseded mesh publication");
    }
    auto dependencies = target.dependencies;
    if (!dependenciesCurrent(target))
    {
        throw std::logic_error("mesh dependencies changed");
    }
    const auto handle = std::get<asset::MeshAssetHandle>(target.asset);
    if (tryMesh(handle))
    {
        device.waitIdle();
        meshes_[handle.index].mesh = std::move(mesh);
    }
    else
    {
        publishMesh(handle, std::move(mesh));
    }
    meshes_[handle.index].residentDependencies = std::move(dependencies);
    meshes_[handle.index].revision = target.revision;
}

void RenderAssetCache::publishPreparedMeshDependencies(const ResourcePreparationTarget& target)
{
    const auto version = inspectPreparation(target);
    const auto handle = std::get<asset::MeshAssetHandle>(target.asset);
    if (!tryMesh(handle) || version.resident != target.revision ||
        version.requested != target.revision ||
        version.requestedDependencies != target.dependencies || !dependenciesCurrent(target))
    {
        throw std::logic_error("mesh changed before dependency publication");
    }
    auto deps = target.dependencies;
    meshes_[handle.index].residentDependencies = std::move(deps);
}

template <typename Handle, typename Resource>
void RenderAssetCache::publishPrepared(const Device& device,
                                       const ResourcePreparationTarget& target,
                                       std::shared_ptr<const Resource> resource)
{
    const auto version = inspectPreparation(target);
    if (!resource || version.requested != target.revision ||
        version.requestedDependencies != target.dependencies || !dependenciesCurrent(target))
    {
        throw std::logic_error("superseded or unready prepared resource");
    }
    auto deps = target.dependencies;
    const auto handle = std::get<Handle>(target.asset);
    auto& entry = entries(handle).at(handle.index);
    if (entry.resource)
    {
        device.waitIdle();
    }
    entry.resource = std::move(resource);
    entry.revision = target.revision;
    entry.residentDependencies = std::move(deps);
}
void RenderAssetCache::publishPreparedShader(const Device& device,
                                             const ResourcePreparationTarget& target,
                                             std::shared_ptr<const GpuShader> resource)
{
    publishPrepared<asset::ShaderAssetHandle>(device, target, std::move(resource));
}
std::shared_ptr<const GpuShader> RenderAssetCache::shader(asset::ShaderAssetHandle handle) const
{
    const auto& list = entries(handle);
    return handle && handle.index < list.size() &&
                   list[handle.index].generation == handle.generation
               ? list[handle.index].resource
               : nullptr;
}
void RenderAssetCache::publishPreparedShaderProgram(
    const Device& device, const ResourcePreparationTarget& target,
    std::shared_ptr<const GpuShaderProgram> resource)
{
    publishPrepared<asset::ShaderProgramAssetHandle>(device, target, std::move(resource));
}
std::shared_ptr<const GpuShaderProgram> RenderAssetCache::shaderProgram(
    asset::ShaderProgramAssetHandle handle) const
{
    const auto& list = entries(handle);
    return handle && handle.index < list.size() &&
                   list[handle.index].generation == handle.generation
               ? list[handle.index].resource
               : nullptr;
}
void RenderAssetCache::publishPreparedMaterialTemplate(
    const Device& device, const ResourcePreparationTarget& target,
    std::shared_ptr<const GpuMaterialTemplate> resource)
{
    publishPrepared<asset::MaterialTemplateAssetHandle>(device, target, std::move(resource));
}
std::shared_ptr<const GpuMaterialTemplate> RenderAssetCache::materialTemplate(
    asset::MaterialTemplateAssetHandle handle) const
{
    const auto& list = entries(handle);
    return handle && handle.index < list.size() &&
                   list[handle.index].generation == handle.generation
               ? list[handle.index].resource
               : nullptr;
}
void RenderAssetCache::publishPreparedModel(const Device& device,
                                            const ResourcePreparationTarget& target,
                                            std::shared_ptr<const asset::ModelAsset> resource)
{
    publishPrepared<asset::ModelAssetHandle>(device, target, std::move(resource));
}
std::shared_ptr<const asset::ModelAsset> RenderAssetCache::model(
    asset::ModelAssetHandle handle) const
{
    const auto& list = entries(handle);
    return handle && handle.index < list.size() &&
                   list[handle.index].generation == handle.generation
               ? list[handle.index].resource
               : nullptr;
}
void RenderAssetCache::publishPreparedMaterial(
    const Device& device, const ResourcePreparationTarget& target, GpuMaterial material,
    DescriptorPool pool, std::shared_ptr<const GpuMaterialTemplate> materialTemplate,
    std::vector<asset::TextureAssetHandle> textures)
{
    const auto version = inspectPreparation(target);
    if (!material || !pool || !materialTemplate || version.requested != target.revision ||
        version.requestedDependencies != target.dependencies || !dependenciesCurrent(target))
    {
        throw std::logic_error("superseded or unready material preparation");
    }
    auto deps = target.dependencies;
    auto layout = materialTemplate->source();
    const auto handle = std::get<asset::MaterialAssetHandle>(target.asset);
    auto& entry = materials_.at(handle.index);
    if (entry.material)
    {
        device.waitIdle();
    }
    entry.pool.reset();
    entry.material = std::move(material);
    entry.layout = std::move(layout);
    entry.textures = std::move(textures);
    entry.preparedTemplate = std::move(materialTemplate);
    entry.pool = std::move(pool);
    entry.revision = target.revision;
    entry.residentDependencies = std::move(deps);
}

void RenderAssetCache::reset() noexcept
{
    // Preparation owner cancels its ticket before reset. Submitted work retains its own leases.
    domain_.reset();
    meshes_.clear();
    materials_.clear();
    textures_.clear();
    models_.clear();
    templates_.clear();
    programs_.clear();
    shaders_.clear();
}

const Mesh& RenderAssetCache::mesh(asset::MeshAssetHandle handle) const
{
    const Mesh* result = tryMesh(handle);
    if (result == nullptr)
    {
        throw std::out_of_range("MeshAsset is absent from RenderAssetCache");
    }
    return *result;
}

const Mesh* RenderAssetCache::tryMesh(
    asset::MeshAssetHandle handle) const noexcept
{
    if (!handle || handle.index >= meshes_.size())
    {
        return nullptr;
    }
    const MeshEntry& entry = meshes_[handle.index];
    return entry.generation == handle.generation && entry.mesh
        ? &entry.mesh
        : nullptr;
}

const GpuMaterial& RenderAssetCache::material(
    asset::MaterialAssetHandle handle) const
{
    const GpuMaterial* result = tryMaterial(handle);
    if (result == nullptr)
    {
        throw std::out_of_range(
            "MaterialAsset is absent from RenderAssetCache");
    }
    return *result;
}

const GpuMaterial* RenderAssetCache::tryMaterial(
    asset::MaterialAssetHandle handle) const noexcept
{
    if (!handle || handle.index >= materials_.size())
    {
        return nullptr;
    }
    const MaterialEntry& entry = materials_[handle.index];
    return entry.generation == handle.generation && entry.material
        ? &entry.material
        : nullptr;
}

const GpuTexture& RenderAssetCache::texture(
    asset::TextureAssetHandle handle) const
{
    const GpuTexture* result = tryTexture(handle);
    if (result == nullptr)
    {
        throw std::out_of_range(
            "TextureAsset is absent from RenderAssetCache");
    }
    return *result;
}

const GpuTexture* RenderAssetCache::tryTexture(
    asset::TextureAssetHandle handle) const noexcept
{
    if (!handle || handle.index >= textures_.size())
    {
        return nullptr;
    }
    const TextureEntry& entry = textures_[handle.index];
    return entry.generation == handle.generation && entry.texture
        ? &entry.texture
        : nullptr;
}

} // namespace rubia::rhi::vulkan
