#include "vulkan/RenderAssetCache.hpp"

#include "vulkan/Device.hpp"
#include "vulkan/RetiredResources.hpp"

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
    if (!emptyPrepared(shaders_) || !emptyPrepared(programs_) || !emptyPrepared(templates_))
    {
        return false;
    }
    return std::none_of(textures_.begin(), textures_.end(),
                        [](const auto& entry) { return bool(entry.texture); }) &&
           std::none_of(materials_.begin(), materials_.end(),
                        [](const auto& entry) { return bool(entry.resources.material); }) &&
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
            inspectPreparation({const_cast<RenderAssetCache*>(this), render::resourceHandle(dependency.handle),
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

uint64_t RenderAssetCache::texturePublication(asset::TextureAssetHandle handle) const noexcept
{
    return tryTexture(handle) ? textures_[handle.index].publication : 0;
}

struct RenderAssetCache::TextureRebindPlan
{
    struct Update
    {
        std::size_t index = 0;
        uint32_t generation = 0;
        VkDescriptorSet originalSet = VK_NULL_HANDLE;
        GpuMaterial::ParameterSnapshot parameters;
        std::vector<GpuTexture> textureOwners;
        MaterialResources resources;
        std::vector<asset::AssetDependencyVersion> dependencies;
        std::vector<asset::AssetDependencyVersion> originalDependencies;
    };
    asset::TextureAssetHandle handle;
    uint64_t originalPublication = 0;
    asset::AssetContentRevision revision = 0;
    std::vector<Update> materials;
    bool built = false;
};
std::shared_ptr<RenderAssetCache::TextureRebindPlan> RenderAssetCache::captureTextureRebind(
    asset::TextureAssetHandle handle, asset::AssetContentRevision revision) const
{
    auto plan = std::make_shared<TextureRebindPlan>();
    plan->handle = handle;
    plan->revision = revision;
    plan->originalPublication = texturePublication(handle);
    for (std::size_t index = 0; index < materials_.size(); ++index)
    {
        const auto &entry = materials_[index];
        const auto &current = entry.resources;
        if (!current.material || std::find(current.textures.begin(), current.textures.end(),
                                           handle) == current.textures.end())
            continue;
        TextureRebindPlan::Update update;
        update.index = index;
        update.generation = entry.generation;
        update.originalSet = current.material.descriptorSet();
        update.parameters = current.material.parameterSnapshot();
        update.resources.layout = current.layout;
        update.resources.textures = current.textures;
        update.resources.preparedTemplate = current.preparedTemplate;
        for (auto textureHandle : current.textures)
            update.textureOwners.push_back(texture(textureHandle).snapshot());
        update.originalDependencies = entry.residentDependencies;
        update.dependencies = entry.residentDependencies;
        for (auto &dependency : update.dependencies)
            if (dependency.handle == asset::AnyAssetHandle(handle))
                dependency.revision = revision;
        plan->materials.push_back(std::move(update));
    }
    return plan;
}
void RenderAssetCache::buildTextureRebind(const Device &device, TextureRebindPlan &plan,
                                          const GpuTexture &replacement)
{
    if (plan.built || !replacement)
        throw std::logic_error("texture rebind plan requires one creation and a valid replacement");
    for (auto &update : plan.materials)
    {
        auto &resources = update.resources;
        resources.pool = resources.preparedTemplate->createDescriptorPool(device);
        const auto sets = resources.pool.allocate(resources.preparedTemplate->layout(), 1);
        std::vector<const GpuTexture *> textures;
        for (std::size_t i = 0; i < resources.textures.size(); ++i)
            textures.push_back(resources.textures[i] == plan.handle ? &replacement
                                                                    : &update.textureOwners[i]);
        resources.material = GpuMaterial::fromParameters(device, resources.layout, textures,
                                                         sets.front(), update.parameters);
    }
    plan.built = true;
}

void RenderAssetCache::publishPreparedTexture(RetiredResources &retired,
                                              const ResourcePreparationTarget &target,
                                              GpuTexture texture,
                                              std::shared_ptr<TextureRebindPlan> rebind)
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
    if (!rebind || !texture)
        throw std::invalid_argument("texture publication requires a prepared binding snapshot");
    {
        // The set of affected materials and each binding version must still match.
        // Validate before moving any live owner; a race fails without partial publication.
        if (!rebind->built || rebind->handle != handle || rebind->revision != target.revision ||
            texturePublication(handle) != rebind->originalPublication || !nextPublication_)
            throw std::runtime_error("texture binding snapshot changed before publication");
        std::size_t match = 0;
        for (std::size_t i = 0; i < materials_.size(); ++i)
        {
            const auto &entry = materials_[i];
            const auto &resources = entry.resources;
            if (!resources.material ||
                std::find(resources.textures.begin(), resources.textures.end(), handle) ==
                    resources.textures.end())
                continue;
            if (match >= rebind->materials.size())
                throw std::runtime_error(
                    "texture acquired a new material during creation; retry preparation");
            const auto &update = rebind->materials[match++];
            if (update.index != i || update.generation != entry.generation ||
                update.originalSet != resources.material.descriptorSet() ||
                update.originalDependencies != entry.residentDependencies ||
                update.parameters.buffer != resources.material.parameterBufferResource() ||
                update.resources.preparedTemplate != resources.preparedTemplate)
                throw std::runtime_error(
                    "material bindings changed during texture creation; retry preparation");
        }
        if (match != rebind->materials.size())
            throw std::runtime_error("texture material snapshot is obsolete");
        if (!rebind->originalPublication)
        {
            publishTexture(handle, std::move(texture));
        }
        else
        {
            struct RetiredReplacement
            {
                GpuTexture texture;
                std::shared_ptr<TextureRebindPlan> bindings;
            };
            auto &previous =
                retired.retire(RetiredReplacement{std::move(texture), std::move(rebind)});
            for (auto &update : previous.bindings->materials)
            {
                auto &entry = materials_[update.index];
                std::swap(entry.resources, update.resources);
                entry.residentDependencies.swap(update.dependencies);
            }
            auto &entry = textures_.at(handle.index); // Admission already reserves the slot.
            std::swap(entry.texture, previous.texture);
            entry.publication = nextPublication_++;
        }
    }
    textures_[handle.index].residentDependencies = std::move(dependencies);
    textures_[handle.index].revision = target.revision;
    textures_[handle.index].requested = target.revision;
}

void RenderAssetCache::publishPreparedMesh(RetiredResources &retired,
                                           const ResourcePreparationTarget &target, Mesh mesh)
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
        retired.retire(std::move(meshes_[handle.index].mesh));
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
void RenderAssetCache::publishPrepared(RetiredResources& retired,
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
        retired.retire(std::move(entry.resource));
    }
    entry.resource = std::move(resource);
    entry.revision = target.revision;
    entry.residentDependencies = std::move(deps);
}
void RenderAssetCache::publishPreparedShader(RetiredResources& retired,
                                             const ResourcePreparationTarget& target,
                                             std::shared_ptr<const GpuShader> resource)
{
    publishPrepared<asset::ShaderAssetHandle>(retired, target, std::move(resource));
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
    RetiredResources& retired, const ResourcePreparationTarget& target,
    std::shared_ptr<const GpuShaderProgram> resource)
{
    publishPrepared<asset::ShaderProgramAssetHandle>(retired, target, std::move(resource));
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
    RetiredResources& retired, const ResourcePreparationTarget& target,
    std::shared_ptr<const GpuMaterialTemplate> resource)
{
    publishPrepared<asset::MaterialTemplateAssetHandle>(retired, target, std::move(resource));
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
void RenderAssetCache::publishPreparedMaterial(
    RetiredResources& retired, const ResourcePreparationTarget& target, GpuMaterial material,
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
    if (entry.resources.material)
    {
        retired.retire(std::move(entry.resources));
    }
    entry.resources.material = std::move(material);
    entry.resources.layout = std::move(layout);
    entry.resources.textures = std::move(textures);
    entry.resources.preparedTemplate = std::move(materialTemplate);
    entry.resources.pool = std::move(pool);
    entry.revision = target.revision;
    entry.residentDependencies = std::move(deps);
}

void RenderAssetCache::reset() noexcept
{
    // Caller completes submitted render uses before resetting live resources.
    // Preparation owner cancels its tickets; uploads retain their own leases.
    domain_.reset();
    meshes_.clear();
    materials_.clear();
    textures_.clear();
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
    return entry.generation == handle.generation && entry.resources.material
        ? &entry.resources.material
        : nullptr;
}

std::shared_ptr<const GpuMaterialTemplate> RenderAssetCache::materialTemplateForMaterial(
    asset::MaterialAssetHandle handle) const
{
    if (!tryMaterial(handle))
        throw std::out_of_range("MaterialAsset is absent from RenderAssetCache");
    return materials_[handle.index].resources.preparedTemplate;
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
