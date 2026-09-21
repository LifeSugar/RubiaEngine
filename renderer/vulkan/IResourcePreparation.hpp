#pragma once
#include "asset/AssetFwd.hpp"
#include "render/ResourcePreparationOptions.hpp"
#include "render/ResourcePreparationSource.hpp"
#include "vulkan/VulkanUploadTypes.hpp"
#include <memory>
#include <variant>

namespace rubia::rhi::vulkan
{
class Device;
class RenderAssetCache;
class RetiredResources;
// Versioned initial publication/replacement. One cache is bound to one asset domain.
struct ResourcePreparationTarget
{
    RenderAssetCache *cache = nullptr;
    render::ResourceAssetHandle asset;
    asset::AssetContentRevision revision = 0;
    asset::AssetDomainId domain;
    std::vector<asset::AssetDependencyVersion> dependencies;
};
// One resource's type-specific work. Owns unpublished GPU objects with RAII.
// Main thread: dependencies, captureDependencies, publish. Worker: create/build.
// captureDependencies pins exact GPU versions; create/build must never access live caches.
// Ownership transfers exclusively to the worker until completion; cancellation never destroys
// an operation while it is executing. publish runs at most once after uploads complete.
// publish must leave the cache unchanged on failure. No ticking, fences, or ticket management here.
class IResourcePreparation
{
  public:
    virtual ~IResourcePreparation() = default;
    virtual std::vector<render::ResourceAssetSnapshot> dependencies() const
    {
        return {};
    }
    virtual void captureDependencies()
    {
    }
    virtual void createGpuResources(const Device &device) = 0;
    // Empty means no transfer is needed; the manager skips UploadService in this case.
    virtual UploadRequest buildUploadRequest() = 0;
    // Replaced objects are handed to the caller; no frame/fence knowledge here.
    virtual void publish(RetiredResources &retired) = 0;
};
std::unique_ptr<IResourcePreparation> makeTexturePreparation(
    RenderAssetCache &cache, asset::AssetSnapshot<asset::TextureAsset> source);
std::unique_ptr<IResourcePreparation> makeMeshPreparation(
    RenderAssetCache &cache, asset::AssetSnapshot<asset::MeshAsset> source);
ResourcePreparationTarget preparationTarget(RenderAssetCache &cache,
                                            const render::ResourceAssetSnapshot &source);
std::unique_ptr<IResourcePreparation> makeShaderPreparation(
    RenderAssetCache &cache, asset::AssetSnapshot<asset::ShaderAsset> source);
std::unique_ptr<IResourcePreparation> makeShaderProgramPreparation(
    RenderAssetCache &cache, asset::AssetSnapshot<asset::ShaderProgramAsset> source);
std::unique_ptr<IResourcePreparation> makeMaterialTemplatePreparation(
    RenderAssetCache &cache, asset::AssetSnapshot<asset::MaterialTemplateAsset> source);
std::unique_ptr<IResourcePreparation> makeMaterialPreparation(
    RenderAssetCache &cache, asset::AssetSnapshot<asset::MaterialAsset> source,
    render::MaterialPreparationOptions options = {});

} // namespace rubia::rhi::vulkan
