#include "vulkan/Device.hpp"
#include "vulkan/IResourcePreparation.hpp"
#include "vulkan/RenderAssetCache.hpp"
#include "vulkan/TextureUploadBuilder.hpp"
#include <functional>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>

namespace rubia::rhi::vulkan
{
ResourcePreparationTarget preparationTarget(RenderAssetCache& cache,
                                            const asset::AnyAssetSnapshot& source)
{
    auto result = std::visit(
        [&](const auto& value)
        {
            if (!value)
            {
                throw std::invalid_argument("invalid asset snapshot");
            }
            return ResourcePreparationTarget{&cache, value.version.handle,
                                             value.version.contentRevision, value.domain};
        },
        source);
    using Key = std::tuple<size_t, uint32_t, uint32_t>;
    std::map<Key, asset::AssetDependencyVersion> versions;
    std::set<Key> active;
    std::function<void(const asset::AnyAssetSnapshot&, bool)> walk;
    walk = [&](const asset::AnyAssetSnapshot& node, bool root)
    {
        std::visit(
            [&](const auto& value)
            {
                if (!value || value.domain != result.domain)
                {
                    throw std::invalid_argument("invalid dependency snapshot/domain");
                }
                const asset::AnyAssetHandle handle = value.version.handle;
                const Key key{handle.index(), value.version.handle.index,
                              value.version.handle.generation};
                if (!active.insert(key).second)
                {
                    throw std::invalid_argument("cyclic preparation dependencies");
                }
                if (!root)
                {
                    const auto inserted = versions.emplace(
                        key, asset::AssetDependencyVersion{handle, value.version.contentRevision});
                    if (!inserted.second &&
                        inserted.first->second.revision != value.version.contentRevision)
                    {
                        throw std::invalid_argument("conflicting dependency versions");
                    }
                }
                if (value.dependencies)
                {
                    for (const auto& child : value.dependencies->direct)
                    {
                        walk(child, false);
                    }
                }
                active.erase(key);
            },
            node);
    };
    walk(source, true);
    for (const auto& pair : versions)
    {
        result.dependencies.push_back(pair.second);
    }
    return result;
}
namespace
{
template <typename Asset> class PreparationBase : public IResourcePreparation
{
public:
    PreparationBase(RenderAssetCache& cache, asset::AssetSnapshot<Asset> source)
        : cache_(cache), target_(preparationTarget(cache, source)), source_(std::move(source.data)),
          dependencies_(std::move(source.dependencies))
    {
    }
    std::vector<asset::AnyAssetSnapshot> dependencies() const override
    {
        return dependencies_ ? dependencies_->direct : std::vector<asset::AnyAssetSnapshot>{};
    }
    UploadRequest buildUploadRequest() override
    {
        return {};
    }

protected:
    RenderAssetCache& cache_;
    ResourcePreparationTarget target_;
    std::shared_ptr<const Asset> source_;
    std::shared_ptr<const asset::AssetSnapshotDependencies> dependencies_;
    const Device* device_ = nullptr;
};
class TexturePreparation final : public PreparationBase<asset::TextureAsset>
{
public:
    using PreparationBase::PreparationBase;
    void createGpuResources(const Device& device) override
    {
        device_ = &device;
        texture_ = std::make_shared<GpuTexture>();
        texture_->allocate(device, makeTextureCreateInfo(*source_));
    }
    UploadRequest buildUploadRequest() override
    {
        auto request = makeTextureUploadRequest(texture_, source_);
        source_.reset();
        return request;
    }
    void publish() override
    {
        cache_.publishPreparedTexture(*device_, target_, std::move(*texture_));
    }

private:
    std::shared_ptr<GpuTexture> texture_;
};
class MeshPreparation final : public PreparationBase<asset::MeshAsset>
{
public:
    using PreparationBase::PreparationBase;
    void createGpuResources(const Device& device) override
    {
        device_ = &device;
        // Dependency changes do not change geometry bytes or submesh handles.
        if (cache_.inspectPreparation(target_).resident == target_.revision)
        {
            reuseGeometry_ = true;
            return;
        }
        mesh_ = std::make_shared<Mesh>();
        mesh_->allocate(device, *source_);
    }
    UploadRequest buildUploadRequest() override
    {
        if (reuseGeometry_)
        {
            return {};
        }
        auto request = Mesh::makeUploadRequest(mesh_, source_);
        source_.reset();
        return request;
    }
    void publish() override
    {
        if (reuseGeometry_)
        {
            cache_.publishPreparedMeshDependencies(target_);
        }
        else
        {
            cache_.publishPreparedMesh(*device_, target_, std::move(*mesh_));
        }
    }

private:
    std::shared_ptr<Mesh> mesh_;
    bool reuseGeometry_ = false;
};
class ShaderPreparation final : public PreparationBase<asset::ShaderAsset>
{
public:
    using PreparationBase::PreparationBase;
    void createGpuResources(const Device& device) override
    {
        device_ = &device;
        shader_ = std::make_shared<GpuShader>(device, source_);
    }
    void publish() override
    {
        cache_.publishPreparedShader(*device_, target_, std::move(shader_));
    }

private:
    std::shared_ptr<const GpuShader> shader_;
};
class ShaderProgramPreparation final : public PreparationBase<asset::ShaderProgramAsset>
{
public:
    using PreparationBase::PreparationBase;
    void createGpuResources(const Device& device) override
    {
        device_ = &device;
        std::vector<std::shared_ptr<const GpuShader>> shaders;
        for (auto handle : source_->shaders())
        {
            shaders.push_back(cache_.shader(handle));
        }
        program_ = std::make_shared<GpuShaderProgram>(device, source_, std::move(shaders));
    }
    void publish() override
    {
        cache_.publishPreparedShaderProgram(*device_, target_, std::move(program_));
    }

private:
    std::shared_ptr<const GpuShaderProgram> program_;
};
class MaterialTemplatePreparation final : public PreparationBase<asset::MaterialTemplateAsset>
{
public:
    using PreparationBase::PreparationBase;
    void createGpuResources(const Device& device) override
    {
        device_ = &device;
        layout_ = std::make_shared<GpuMaterialTemplate>(device, source_,
                                                        cache_.shaderProgram(source_->program()));
    }
    void publish() override
    {
        cache_.publishPreparedMaterialTemplate(*device_, target_, std::move(layout_));
    }

private:
    std::shared_ptr<const GpuMaterialTemplate> layout_;
};
class MaterialPreparation final : public PreparationBase<asset::MaterialAsset>
{
public:
    using PreparationBase::PreparationBase;
    void createGpuResources(const Device& device) override
    {
        device_ = &device;
        layout_ = cache_.materialTemplate(source_->materialTemplate());
        if (!layout_)
        {
            throw std::invalid_argument("material template is not prepared");
        }
        const auto& cpuLayout = layout_->source();
        std::map<VkDescriptorType, uint32_t> counts;
        for (const auto& binding : cpuLayout.bindings())
        {
            VkDescriptorType type;
            switch (binding.type)
            {
            case asset::ShaderResourceType::UniformBuffer:
                type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                break;
            case asset::ShaderResourceType::SampledImage:
                type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                break;
            case asset::ShaderResourceType::Sampler:
                type = VK_DESCRIPTOR_TYPE_SAMPLER;
                break;
            default:
                throw std::invalid_argument("unsupported material descriptor type");
            }
            counts[type] += binding.arrayCount;
        }
        std::vector<VkDescriptorPoolSize> sizes;
        for (const auto& pair : counts)
        {
            sizes.push_back({pair.first, pair.second});
        }
        pool_.create(device.get(), sizes, 1);
        const auto sets = pool_.allocate(layout_->layout(), 1);
        std::vector<const GpuTexture*> textures;
        for (auto handle : source_->textures())
        {
            textures.push_back(cache_.tryTexture(handle));
        }
        material_.create(device, *source_, cpuLayout, textures, sets.front());
    }
    void publish() override
    {
        cache_.publishPreparedMaterial(*device_, target_, std::move(material_), std::move(pool_),
                                       layout_, source_->textures());
    }

private:
    std::shared_ptr<const GpuMaterialTemplate> layout_;
    GpuMaterial material_;
    DescriptorPool pool_;
};
class ModelPreparation final : public PreparationBase<asset::ModelAsset>
{
public:
    using PreparationBase::PreparationBase;
    void createGpuResources(const Device& device) override
    {
        device_ = &device;
        // Model is a hierarchy. Its transitive mesh/material dependencies carry GPU objects.
        for (const auto& node : source_->nodes())
        {
            for (auto mesh : node.meshes)
            {
                if (!cache_.tryMesh(mesh))
                {
                    throw std::invalid_argument("model mesh is not prepared");
                }
            }
        }
    }
    void publish() override
    {
        cache_.publishPreparedModel(*device_, target_, source_);
    }
};
} // namespace
std::unique_ptr<IResourcePreparation> makeTexturePreparation(
    RenderAssetCache& cache, asset::AssetSnapshot<asset::TextureAsset> source)
{
    if (!source)
    {
        throw std::invalid_argument("invalid Texture snapshot");
    }
    return std::make_unique<TexturePreparation>(cache, std::move(source));
}
std::unique_ptr<IResourcePreparation> makeMeshPreparation(
    RenderAssetCache& cache, asset::AssetSnapshot<asset::MeshAsset> source)
{
    if (!source)
    {
        throw std::invalid_argument("invalid Mesh snapshot");
    }
    return std::make_unique<MeshPreparation>(cache, std::move(source));
}
std::unique_ptr<IResourcePreparation> makeShaderPreparation(
    RenderAssetCache& cache, asset::AssetSnapshot<asset::ShaderAsset> source)
{
    if (!source)
    {
        throw std::invalid_argument("invalid Shader snapshot");
    }
    return std::make_unique<ShaderPreparation>(cache, std::move(source));
}
std::unique_ptr<IResourcePreparation> makeShaderProgramPreparation(
    RenderAssetCache& cache, asset::AssetSnapshot<asset::ShaderProgramAsset> source)
{
    if (!source)
    {
        throw std::invalid_argument("invalid ShaderProgram snapshot");
    }
    return std::make_unique<ShaderProgramPreparation>(cache, std::move(source));
}
std::unique_ptr<IResourcePreparation> makeMaterialTemplatePreparation(
    RenderAssetCache& cache, asset::AssetSnapshot<asset::MaterialTemplateAsset> source)
{
    if (!source)
    {
        throw std::invalid_argument("invalid MaterialTemplate snapshot");
    }
    return std::make_unique<MaterialTemplatePreparation>(cache, std::move(source));
}
std::unique_ptr<IResourcePreparation> makeMaterialPreparation(
    RenderAssetCache& cache, asset::AssetSnapshot<asset::MaterialAsset> source)
{
    if (!source)
    {
        throw std::invalid_argument("invalid Material snapshot");
    }
    return std::make_unique<MaterialPreparation>(cache, std::move(source));
}
std::unique_ptr<IResourcePreparation> makeModelPreparation(
    RenderAssetCache& cache, asset::AssetSnapshot<asset::ModelAsset> source)
{
    if (!source)
    {
        throw std::invalid_argument("invalid Model snapshot");
    }
    return std::make_unique<ModelPreparation>(cache, std::move(source));
}
} // namespace rubia::rhi::vulkan
