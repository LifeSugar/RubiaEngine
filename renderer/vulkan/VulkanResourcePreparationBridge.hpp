#pragma once

#include "render/ResourcePreparation.hpp"
#include <unordered_set>

namespace rubia::rhi::vulkan
{
class VulkanRenderer;
class RenderAssetCache;

// Composition-root adapter. Renderer/cache must outlive it; cancelAll before
// resetting either. It tracks caller ownership, not another preparation task.
class VulkanResourcePreparationBridge final : public render::ResourcePreparation
{
  public:
    VulkanResourcePreparationBridge(VulkanRenderer& renderer, RenderAssetCache& cache);
    ~VulkanResourcePreparationBridge() override;
    VulkanResourcePreparationBridge(const VulkanResourcePreparationBridge&) = delete;
    VulkanResourcePreparationBridge& operator=(const VulkanResourcePreparationBridge&) = delete;

    using render::ResourcePreparation::prepare;
    render::ResourcePreparationResult prepare(render::ResourceAssetSnapshot source) override;
    render::ResourcePreparationStatus status(
        render::ResourcePreparationTicket ticket) const override;
    void cancel(render::ResourcePreparationTicket ticket) override;
    void release(render::ResourcePreparationTicket ticket) override;
    render::PipelinePreparationResult prewarmPipelines(
        const std::vector<asset::MaterialAssetHandle>& materials) override;
    render::PipelinePreparationStatus pipelineStatus(render::PipelinePreparationTicket ticket) const override;
    void cancelPipelines(render::PipelinePreparationTicket ticket) override;
    void releasePipelines(render::PipelinePreparationTicket ticket) override;
    void setPublicationTransaction(render::ResourcePreparationTicket,
        std::shared_ptr<render::ResourcePublicationTransaction>) override;
    void cancelAll() noexcept override;
    void advance() override;

  private:
    void checkTicket(render::ResourcePreparationTicket ticket) const;
    VulkanRenderer& renderer_;
    RenderAssetCache& cache_;
    std::unordered_set<uint64_t> tickets_;
    std::unordered_set<uint64_t> pipelineTickets_;
    void checkPipelineTicket(render::PipelinePreparationTicket ticket) const;
};
} // namespace rubia::rhi::vulkan
