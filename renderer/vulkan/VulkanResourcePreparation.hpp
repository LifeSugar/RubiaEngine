#pragma once
#include "vulkan/IResourcePreparation.hpp"
#include "vulkan/ResourcePreparationTypes.hpp"
#include <map>
#include <optional>
#include <thread>

namespace rubia::rhi::vulkan
{
class GpuTexture;
class VulkanUploadService;
// Render-thread-only. Device/service outlive the manager; caches outlive their requests
// and all published GPU uses. Source bytes must remain immutable while retained.
// Scene admission and the once-per-frame UploadService::tick stay with Renderer.
class VulkanResourcePreparation final
{
public:
    VulkanResourcePreparation(const Device& device, VulkanUploadService& uploads,
                              std::size_t maxRequests = 1024);
    ~VulkanResourcePreparation();
    VulkanResourcePreparation(const VulkanResourcePreparation&) = delete;
    VulkanResourcePreparation& operator=(const VulkanResourcePreparation&) = delete;
    ResourcePreparationResult prepareTexture(RenderAssetCache& cache,
                                             asset::AssetSnapshot<asset::TextureAsset> source);
    ResourcePreparationResult prepareMesh(RenderAssetCache& cache,
                                          asset::AssetSnapshot<asset::MeshAsset> source);
    ResourcePreparationResult prepareShader(RenderAssetCache& cache,
                                            asset::AssetSnapshot<asset::ShaderAsset> source);
    ResourcePreparationResult prepareShaderProgram(
        RenderAssetCache& cache, asset::AssetSnapshot<asset::ShaderProgramAsset> source);
    ResourcePreparationResult prepareMaterialTemplate(
        RenderAssetCache& cache, asset::AssetSnapshot<asset::MaterialTemplateAsset> source);
    ResourcePreparationResult prepareMaterial(RenderAssetCache& cache,
                                              asset::AssetSnapshot<asset::MaterialAsset> source);
    ResourcePreparationResult prepareModel(RenderAssetCache& cache,
                                           asset::AssetSnapshot<asset::ModelAsset> source);
    // Backend extension point. The operation must publish into the supplied target.
    // Takes ownership even on rejection; source callers retain their own immutable assets for
    // retry.
    ResourcePreparationResult prepare(ResourcePreparationTarget target,
                                      std::unique_ptr<IResourcePreparation> preparation);
    // Call at a frame boundary before recording draws. Never ticks/drains the service;
    // replacement publication currently waits for submitted GPU users before committing.
    void advance();
    ResourcePreparationStatus status(ResourcePreparationTicket ticket) const;
    // Cancels only this subscriber; the last cancellation retires the shared task.
    void cancel(ResourcePreparationTicket ticket);
    void release(ResourcePreparationTicket ticket); // Terminal preparation records only.
    [[nodiscard]] GpuTexture uploadTextureAndWait(
        std::shared_ptr<const asset::TextureAsset> source);
    [[nodiscard]] bool empty() const
    {
        checkThread();
        return records_.empty();
    }

private:
    // One shared task can have multiple independently cancellable caller tickets.
    struct PreparationRecord
    {
        ResourcePreparationTarget target;
        std::unique_ptr<IResourcePreparation> preparation;
        UploadTicket upload;
        ResourcePreparationStatus status;
        std::size_t subscribers = 0;
        struct Dependency
        {
            asset::AnyAssetSnapshot source;
            ResourcePreparationTicket ticket;
            bool ready = false;
        };
        std::vector<Dependency> dependencies;
        UploadRequest pendingUpload;
        bool created = false;
    };
    struct Subscription
    {
        std::shared_ptr<PreparationRecord> task;
        std::optional<ResourcePreparationStatus> cancelled;
        bool internal = false;
    };
    ResourcePreparationResult prepareAny(RenderAssetCache& cache, asset::AnyAssetSnapshot source);
    bool advanceDependencies(PreparationRecord& record);
    void checkThread() const;
    void retire(PreparationRecord& record);
    ResourcePreparationStatus taskStatus(const PreparationRecord& record) const;
    const Device& device_;
    VulkanUploadService& uploads_;
    std::size_t maxRequests_;
    std::thread::id thread_;
    uint64_t nextTicket_ = 1;
    bool dependencyAdmission_ = false;
    std::map<uint64_t, Subscription> records_;
    std::map<uint64_t, std::shared_ptr<PreparationRecord>> tasks_;
};
} // namespace rubia::rhi::vulkan
