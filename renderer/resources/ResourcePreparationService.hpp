#pragma once
#include "resources/IResourcePreparation.hpp"
#include "render/ResourcePreparation.hpp"
#include "rhi/UploadService.hpp"
#include "resources/RetirementBatch.hpp"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <exception>
#include <map>
#include <mutex>
#include <optional>
#include <thread>

namespace rubia::render
{
// Render-thread scheduling/publication, dedicated worker for GPU object creation.
// Device/service outlive the manager (destruction joins); caches outlive their requests
// and all published GPU uses. Source bytes must remain immutable while retained.
// Scene admission and the once-per-frame UploadService::tick stay with Renderer.
// The retirement batch outlives the manager; its owner clears it only after all
// prior graphics uses complete. No publication may interleave draw recording.
class ResourcePreparationService
{
  public:
    ResourcePreparationService(const rhi::Device &device, rhi::UploadService &uploads,
                               RetirementBatch &retired,
                               render::ResourcePreparationOptions options = {},
                               std::size_t maxRequests = 1024);
    virtual ~ResourcePreparationService();
    ResourcePreparationService(const ResourcePreparationService &) = delete;
    ResourcePreparationService &operator=(const ResourcePreparationService &) = delete;
    ResourcePreparationResult prepareTexture(RenderAssetStore &cache,
                                             asset::AssetSnapshot<asset::TextureAsset> source);
    ResourcePreparationResult prepareMesh(RenderAssetStore &cache,
                                          asset::AssetSnapshot<asset::MeshAsset> source);
    ResourcePreparationResult prepareShader(RenderAssetStore &cache,
                                            asset::AssetSnapshot<asset::ShaderAsset> source);
    ResourcePreparationResult
    prepareShaderProgram(RenderAssetStore &cache,
                         asset::AssetSnapshot<asset::ShaderProgramAsset> source);
    ResourcePreparationResult
    prepareMaterialTemplate(RenderAssetStore &cache,
                            asset::AssetSnapshot<asset::MaterialTemplateAsset> source);
    ResourcePreparationResult prepareMaterial(RenderAssetStore &cache,
                                              asset::AssetSnapshot<asset::MaterialAsset> source);

    // Backend extension point. The operation must publish into the supplied target.
    // Takes ownership even on rejection; source callers retain their own immutable assets for
    // retry.
    ResourcePreparationResult prepare(ResourcePreparationTarget target,
                                      std::unique_ptr<IResourcePreparation> preparation);
    // Call at a frame boundary before recording draws. Never ticks/drains the service;
    // replaced owners go to the caller's retirement batch. Texture replacement
    // creates new descriptor sets and preserves immutable parameter allocations.
    void advance();
    ResourcePreparationStatus status(ResourcePreparationTicket ticket) const;
    // Cancels only this subscriber; the last cancellation retires the shared task.
    void cancel(ResourcePreparationTicket ticket);
    void release(ResourcePreparationTicket ticket); // Terminal preparation records only.
    void setPublicationTransaction(ResourcePreparationTicket,
                                   std::shared_ptr<render::ResourcePublicationTransaction>);
    [[nodiscard]] bool empty() const
    {
        checkThread();
        return records_.empty();
    }

  private:
    struct CreationWork
    {
        std::unique_ptr<IResourcePreparation> preparation;
        rhi::UploadRequest upload;
        std::exception_ptr error;
        std::atomic_bool cancelled{false};
        bool done = false; // Protected by creationMutex_.
    };
    void runCreationWorker() noexcept;
    struct PreparationRecord;
    bool advanceCreation(PreparationRecord &record);
    // One shared task can have multiple independently cancellable caller tickets.
    struct PreparationRecord
    {
        ResourcePreparationTarget target;
        std::unique_ptr<IResourcePreparation> preparation;
        rhi::UploadTicket upload;
        ResourcePreparationStatus status;
        std::size_t subscribers = 0;
        struct Dependency
        {
            render::ResourceAssetSnapshot source;
            ResourcePreparationTicket ticket;
            bool ready = false;
        };
        std::vector<Dependency> dependencies;
        rhi::UploadRequest pendingUpload;
        std::shared_ptr<render::ResourcePublicationTransaction> publication;
        bool created = false;
        std::shared_ptr<CreationWork> creation;
    };
    struct Subscription
    {
        std::shared_ptr<PreparationRecord> task;
        std::optional<ResourcePreparationStatus> cancelled;
        bool internal = false;
    };
    ResourcePreparationResult prepareAny(RenderAssetStore &cache,
                                         render::ResourceAssetSnapshot source);
    bool advanceDependencies(PreparationRecord &record);
    void checkThread() const;
    void retire(PreparationRecord &record);
    ResourcePreparationStatus taskStatus(const PreparationRecord &record) const;
    const rhi::Device &device_;
    rhi::UploadService &uploads_;
    RetirementBatch &retired_; // Outlives this manager; caller supplies GPU-safe reclamation.
    std::size_t maxRequests_;
    const render::ResourcePreparationOptions options_;
    std::thread::id thread_;
    uint64_t nextTicket_ = 1;
    bool dependencyAdmission_ = false;
    std::map<uint64_t, Subscription> records_;
    std::map<uint64_t, std::shared_ptr<PreparationRecord>> tasks_;
    static constexpr std::size_t maxCreationsInFlight_ = 2;
    std::vector<std::shared_ptr<CreationWork>> activeCreations_;
    std::size_t dispatchedThisAdvance_ = 0;
    std::mutex creationMutex_;
    std::condition_variable creationWake_;
    bool stopping_ = false;
    std::deque<std::shared_ptr<CreationWork>> creationQueue_;
    std::thread creationThread_;
};
} // namespace rubia::render
