#pragma once
#include "asset/AssetFwd.hpp"
#include "asset/AssetSnapshot.hpp"
#include "vulkan/VulkanUploadTypes.hpp"
#include <memory>
#include <variant>

namespace rubia::rhi::vulkan
{
class Device;
class RenderAssetCache;
// Versioned initial publication/replacement. One cache is bound to one asset domain.
struct ResourcePreparationTarget
{
    RenderAssetCache* cache = nullptr;
    asset::AnyAssetHandle asset;
    asset::AssetContentRevision revision = 0;
    asset::AssetDomainId domain;
    std::vector<asset::AssetDependencyVersion> dependencies;
};
// One resource's type-specific work. Owns unpublished GPU objects with RAII.
// The manager calls create/build once, and publish at most once after all uploads complete.
// publish must leave the cache unchanged on failure. No ticking, fences, or ticket management here.
class IResourcePreparation
{
public:
    virtual ~IResourcePreparation() = default;
    virtual std::vector<asset::AnyAssetSnapshot> dependencies() const
    {
        return {};
    }
    virtual void createGpuResources(const Device& device) = 0;
    // Empty means no transfer is needed; the manager skips UploadService in this case.
    virtual UploadRequest buildUploadRequest() = 0;
    virtual void publish() = 0;
};
std::unique_ptr<IResourcePreparation> makeTexturePreparation(
    RenderAssetCache& cache, asset::AssetSnapshot<asset::TextureAsset> source);
std::unique_ptr<IResourcePreparation> makeMeshPreparation(
    RenderAssetCache& cache, asset::AssetSnapshot<asset::MeshAsset> source);
ResourcePreparationTarget preparationTarget(RenderAssetCache& cache,
                                            const asset::AnyAssetSnapshot& source);
std::unique_ptr<IResourcePreparation> makeShaderPreparation(
    RenderAssetCache& cache, asset::AssetSnapshot<asset::ShaderAsset> source);
std::unique_ptr<IResourcePreparation> makeShaderProgramPreparation(
    RenderAssetCache& cache, asset::AssetSnapshot<asset::ShaderProgramAsset> source);
std::unique_ptr<IResourcePreparation> makeMaterialTemplatePreparation(
    RenderAssetCache& cache, asset::AssetSnapshot<asset::MaterialTemplateAsset> source);
std::unique_ptr<IResourcePreparation> makeMaterialPreparation(
    RenderAssetCache& cache, asset::AssetSnapshot<asset::MaterialAsset> source);
std::unique_ptr<IResourcePreparation> makeModelPreparation(
    RenderAssetCache& cache, asset::AssetSnapshot<asset::ModelAsset> source);
} // namespace rubia::rhi::vulkan
