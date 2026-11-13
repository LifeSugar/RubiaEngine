#include "rhi/Device.hpp"
#include "resources/IResourcePreparation.hpp"
#include "resources/RenderAssetStore.hpp"
#include "resources/TextureUploadBuilder.hpp"
#include <functional>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>

namespace rubia::render
{
using rhi::BufferUpload;
using rhi::UploadRequest;
ResourcePreparationTarget preparationTarget(RenderAssetStore &cache,
                                            const render::ResourceAssetSnapshot &source)
{
    auto result = std::visit(
        [&](const auto &value)
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
    std::function<void(const render::ResourceAssetSnapshot &, bool)> walk;
    walk = [&](const render::ResourceAssetSnapshot &node, bool root)
    {
        std::visit(
            [&](const auto &value)
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
                    for (const auto &child : value.dependencies->direct)
                    {
                        walk(render::resourceSnapshot(child), false);
                    }
                }
                active.erase(key);
            },
            node);
    };
    walk(source, true);
    for (const auto &pair : versions)
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
    PreparationBase(RenderAssetStore &cache, asset::AssetSnapshot<Asset> source)
        : cache_(cache), target_(preparationTarget(cache, source)), source_(std::move(source.data)),
          dependencies_(std::move(source.dependencies))
    {
    }
    std::vector<render::ResourceAssetSnapshot> dependencies() const override
    {
        std::vector<render::ResourceAssetSnapshot> result;
        if (dependencies_)
            for (const auto &source : dependencies_->direct)
                result.push_back(render::resourceSnapshot(source));
        return result;
    }
    UploadRequest buildUploadRequest() override { return {}; }

  protected:
    RenderAssetStore &cache_;
    ResourcePreparationTarget target_;
    std::shared_ptr<const Asset> source_;
    std::shared_ptr<const asset::AssetSnapshotDependencies> dependencies_;
    const rhi::Device *device_ = nullptr;
};
class TexturePreparation final : public PreparationBase<asset::TextureAsset>
{
  public:
    using PreparationBase::PreparationBase;
    void captureDependencies() override
    {
        rebind_ = cache_.captureTextureRebind(std::get<asset::TextureAssetHandle>(target_.asset),
                                              target_.revision);
    }
    void createGpuResources(const rhi::Device &device) override
    {
        device_ = &device;
        texture_ = std::make_shared<TextureResource>();
        texture_->allocate(device, makeTextureCreateInfo(*source_));
        RenderAssetStore::buildTextureRebind(device, *rebind_, *texture_);
    }
    UploadRequest buildUploadRequest() override
    {
        auto request = makeTextureUploadRequest(texture_, source_);
        source_.reset();
        return request;
    }
    void publish(RetirementBatch &retired) override
    {
        cache_.publishPreparedTexture(retired, target_, std::move(*texture_), std::move(rebind_));
    }

  private:
    std::shared_ptr<RenderAssetStore::TextureRebindPlan> rebind_;
    std::shared_ptr<TextureResource> texture_;
};
class MeshPreparation final : public PreparationBase<asset::MeshAsset>
{
  public:
    using PreparationBase::PreparationBase;
    void captureDependencies() override
    {
        // Main thread decides whether the current geometry can be reused.
        reuseGeometry_ = cache_.inspectPreparation(target_).resident == target_.revision;
    }
    void createGpuResources(const rhi::Device &device) override
    {
        device_ = &device;
        if (reuseGeometry_)
            return;
        mesh_ = std::make_shared<MeshGeometry>();
        mesh_->allocate(device, *source_);
    }
    UploadRequest buildUploadRequest() override
    {
        if (reuseGeometry_)
        {
            return {};
        }
        auto request = MeshGeometry::makeUploadRequest(mesh_, source_);
        source_.reset();
        return request;
    }
    void publish(RetirementBatch &retired) override
    {
        if (reuseGeometry_)
        {
            cache_.publishPreparedMeshDependencies(target_);
        }
        else
        {
            cache_.publishPreparedMesh(retired, target_, std::move(*mesh_));
        }
    }

  private:
    std::shared_ptr<MeshGeometry> mesh_;
    bool reuseGeometry_ = false;
};
class ShaderPreparation final : public PreparationBase<asset::ShaderAsset>
{
  public:
    using PreparationBase::PreparationBase;
    void createGpuResources(const rhi::Device &device) override
    {
        device_ = &device;
        shader_ = std::make_shared<PreparedShader>(device, source_);
    }
    void publish(RetirementBatch &retired) override
    {
        cache_.publishPreparedShader(retired, target_, std::move(shader_));
    }

  private:
    std::shared_ptr<const PreparedShader> shader_;
};
class ShaderProgramPreparation final : public PreparationBase<asset::ShaderProgramAsset>
{
  public:
    using PreparationBase::PreparationBase;
    void captureDependencies() override
    {
        for (auto handle : source_->shaders())
            shaders_.push_back(cache_.shader(handle));
    }
    void createGpuResources(const rhi::Device &device) override
    {
        device_ = &device;
        program_ = std::make_shared<PreparedShaderProgram>(device, source_, std::move(shaders_));
    }
    void publish(RetirementBatch &retired) override
    {
        cache_.publishPreparedShaderProgram(retired, target_, std::move(program_));
    }

  private:
    std::vector<std::shared_ptr<const PreparedShader>> shaders_;
    std::shared_ptr<const PreparedShaderProgram> program_;
};
class MaterialTemplatePreparation final : public PreparationBase<asset::MaterialTemplateAsset>
{
  public:
    using PreparationBase::PreparationBase;
    void captureDependencies() override { program_ = cache_.shaderProgram(source_->program()); }
    void createGpuResources(const rhi::Device &device) override
    {
        device_ = &device;
        layout_ = std::make_shared<PreparedMaterialTemplate>(device, source_, program_);
    }
    void publish(RetirementBatch &retired) override
    {
        cache_.publishPreparedMaterialTemplate(retired, target_, std::move(layout_));
    }

  private:
    std::shared_ptr<const PreparedShaderProgram> program_;
    std::shared_ptr<const PreparedMaterialTemplate> layout_;
};
class MaterialPreparation final : public PreparationBase<asset::MaterialAsset>
{
  public:
    MaterialPreparation(RenderAssetStore &cache, asset::AssetSnapshot<asset::MaterialAsset> source,
                        render::MaterialPreparationOptions options)
        : PreparationBase(cache, std::move(source)), options_(options)
    {
        if (!render::validMaterialParameterMemory(options_.parameterMemory))
            throw std::invalid_argument("invalid material parameter memory policy");
    }
    void captureDependencies() override
    {
        layout_ = cache_.materialTemplate(source_->materialTemplate());
        if (!layout_)
        {
            throw std::invalid_argument("material template is not prepared");
        }
        for (auto handle : source_->textures())
            textures_.push_back(cache_.texture(handle).snapshot());
    }
    void createGpuResources(const rhi::Device &device) override
    {
        device_ = &device;
        const auto &cpuLayout = layout_->source();
        std::vector<const TextureResource *> textures;
        for (const auto &texture : textures_)
            textures.push_back(&texture);
        const bool mapped = render::hasMemoryProperty(options_.parameterMemory,
                                                      render::MaterialParameterMemory::HostVisible);
        uint32_t properties = 0;
        if (render::hasMemoryProperty(options_.parameterMemory,
                                      render::MaterialParameterMemory::DeviceLocal))
            properties |= rhi::flag(rhi::MemoryProperty::DeviceLocal);
        if (mapped)
            properties |= rhi::flag(rhi::MemoryProperty::HostVisible);
        const uint32_t usage = rhi::flag(rhi::BufferUsage::Uniform) |
                               (mapped ? 0u : rhi::flag(rhi::BufferUsage::TransferDestination));
        auto parameters = device.createBuffer({source_->parameterData().size(), usage, properties});
        if (mapped)
            device.writeBuffer(parameters, source_->parameterData().data(),
                               source_->parameterData().size());
        material_.create(device, *source_, cpuLayout, textures, layout_->layout(),
                         std::move(parameters));
    }
    UploadRequest buildUploadRequest() override
    {
        if (render::hasMemoryProperty(options_.parameterMemory,
                                      render::MaterialParameterMemory::HostVisible))
            return {}; // Direct writes (and any needed flush) already completed.
        BufferUpload parameters;
        parameters.destination = material_.parameterBufferResource();
        parameters.source = {source_, source_->parameterData().data(),
                             source_->parameterData().size()};
        // A material UBO may be consumed by more than the fragment shader.
        // Use the reflected parameter binding, not texture stages or a fixed pass.
        const auto &cpuLayout = layout_->source();
        const auto descriptor = cpuLayout.parameterBlock().descriptor;
        for (const auto &binding : cpuLayout.bindings())
        {
            if (binding.set != descriptor.set || binding.binding != descriptor.binding)
                continue;
            if (binding.stages & asset::shaderStageMask(asset::ShaderStage::Vertex))
                parameters.after.stages |= rhi::shaderStageMask(rhi::ShaderStage::Vertex);
            if (binding.stages & asset::shaderStageMask(asset::ShaderStage::Fragment))
                parameters.after.stages |= rhi::shaderStageMask(rhi::ShaderStage::Fragment);
            if (binding.stages & asset::shaderStageMask(asset::ShaderStage::Compute))
                parameters.after.stages |= rhi::shaderStageMask(rhi::ShaderStage::Compute);
        }
        if (!parameters.after.stages)
            throw std::invalid_argument("material parameter binding has no shader consumers");
        parameters.after.usage = rhi::AccessUsage::UniformRead;
        UploadRequest request;
        request.operations.emplace_back(std::move(parameters));
        return request;
    }
    void publish(RetirementBatch &retired) override
    {
        cache_.publishPreparedMaterial(retired, target_, std::move(material_), layout_,
                                       source_->textures());
    }

  private:
    const render::MaterialPreparationOptions options_;
    std::shared_ptr<const PreparedMaterialTemplate> layout_;
    std::vector<TextureResource> textures_;
    MaterialBinding material_;
};
} // namespace
std::unique_ptr<IResourcePreparation>
makeTexturePreparation(RenderAssetStore &cache, asset::AssetSnapshot<asset::TextureAsset> source)
{
    if (!source)
    {
        throw std::invalid_argument("invalid Texture snapshot");
    }
    return std::make_unique<TexturePreparation>(cache, std::move(source));
}
std::unique_ptr<IResourcePreparation>
makeMeshPreparation(RenderAssetStore &cache, asset::AssetSnapshot<asset::MeshAsset> source)
{
    if (!source)
    {
        throw std::invalid_argument("invalid Mesh snapshot");
    }
    return std::make_unique<MeshPreparation>(cache, std::move(source));
}
std::unique_ptr<IResourcePreparation>
makeShaderPreparation(RenderAssetStore &cache, asset::AssetSnapshot<asset::ShaderAsset> source)
{
    if (!source)
    {
        throw std::invalid_argument("invalid Shader snapshot");
    }
    return std::make_unique<ShaderPreparation>(cache, std::move(source));
}
std::unique_ptr<IResourcePreparation>
makeShaderProgramPreparation(RenderAssetStore &cache,
                             asset::AssetSnapshot<asset::ShaderProgramAsset> source)
{
    if (!source)
    {
        throw std::invalid_argument("invalid ShaderProgram snapshot");
    }
    return std::make_unique<ShaderProgramPreparation>(cache, std::move(source));
}
std::unique_ptr<IResourcePreparation>
makeMaterialTemplatePreparation(RenderAssetStore &cache,
                                asset::AssetSnapshot<asset::MaterialTemplateAsset> source)
{
    if (!source)
    {
        throw std::invalid_argument("invalid MaterialTemplate snapshot");
    }
    return std::make_unique<MaterialTemplatePreparation>(cache, std::move(source));
}
std::unique_ptr<IResourcePreparation>
makeMaterialPreparation(RenderAssetStore &cache, asset::AssetSnapshot<asset::MaterialAsset> source,
                        render::MaterialPreparationOptions options)
{
    if (!source)
    {
        throw std::invalid_argument("invalid Material snapshot");
    }
    return std::make_unique<MaterialPreparation>(cache, std::move(source), options);
}
} // namespace rubia::render
