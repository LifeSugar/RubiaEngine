#pragma once

#include "render/ResourcePreparation.hpp"
#include "resources/ScenePreparationSession.hpp"
#include <unordered_set>

namespace rubia::render
{
// Frame scheduler supplies admission and owns the concrete preparation service.
// The service is resolved lazily so an application session can exist before device setup.
class ResourcePreparationHost : public ScenePreparationHost
{
  public:
    virtual bool canPrepareResources() const noexcept = 0;
    virtual ResourcePreparationService &resourcePreparationService() = 0;
    virtual void advanceResourcePreparation() = 0;
};

// Application-facing resource session. Host/store must outlive it; cancelAll before
// resetting either. Its ticket sets track caller ownership only; all preparation
// work, dependencies and publication remain in the shared preparation service.
class ResourcePreparationSession final : public render::ResourcePreparation
{
  public:
    ResourcePreparationSession(ResourcePreparationHost &renderer, RenderAssetStore &cache);
    ~ResourcePreparationSession() override;
    ResourcePreparationSession(const ResourcePreparationSession &) = delete;
    ResourcePreparationSession &operator=(const ResourcePreparationSession &) = delete;

    using render::ResourcePreparation::prepare;
    render::ResourcePreparationResult prepare(render::ResourceAssetSnapshot source) override;
    render::ResourcePreparationStatus
    status(render::ResourcePreparationTicket ticket) const override;
    void cancel(render::ResourcePreparationTicket ticket) override;
    void release(render::ResourcePreparationTicket ticket) override;
    render::PipelinePreparationResult
    prewarmPipelines(const std::vector<asset::MaterialAssetHandle> &materials) override;
    render::PipelinePreparationStatus
    pipelineStatus(render::PipelinePreparationTicket ticket) const override;
    void cancelPipelines(render::PipelinePreparationTicket ticket) override;
    void releasePipelines(render::PipelinePreparationTicket ticket) override;
    void
        setPublicationTransaction(render::ResourcePreparationTicket,
                                  std::shared_ptr<render::ResourcePublicationTransaction>) override;
    void cancelAll() noexcept override;
    void advance() override;

  private:
    void checkTicket(render::ResourcePreparationTicket ticket) const;
    ResourcePreparationHost &renderer_;
    RenderAssetStore &cache_;
    std::unordered_set<uint64_t> tickets_;
    std::unordered_set<uint64_t> pipelineTickets_;
    void checkPipelineTicket(render::PipelinePreparationTicket ticket) const;
};
} // namespace rubia::render
