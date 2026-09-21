#pragma once
#include "render/PipelinePreparationTypes.hpp"
#include "vulkan/GraphicsPipelineCache.hpp"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <exception>
#include <map>
#include <mutex>

namespace rubia::rhi::vulkan
{
// Render-thread scheduler with one dedicated PSO creation thread. advance() only
// publishes completions and dispatches immutable snapshots; it never waits for creation.
// Device/cache outlive this service. Destruction joins the worker before releasing inputs.
class VulkanPipelinePreparation final
{
  public:
    VulkanPipelinePreparation(const Device &device, GraphicsPipelineCache &cache,
                              std::size_t maxRequests = 1024, std::size_t pipelinesPerAdvance = 2);
    ~VulkanPipelinePreparation();
    VulkanPipelinePreparation(const VulkanPipelinePreparation &) = delete;
    VulkanPipelinePreparation &operator=(const VulkanPipelinePreparation &) = delete;
    render::PipelinePreparationResult request(
        std::vector<GraphicsPipeline::CreateInfo> descriptions);
    render::PipelinePreparationStatus status(render::PipelinePreparationTicket ticket) const;
    void cancel(render::PipelinePreparationTicket ticket);
    void release(render::PipelinePreparationTicket ticket);
    void cancelAll(); // Keeps terminal statuses queryable until release.
    void advance();

  private:
    // Worker never reads Job, Request, or the application cache. Result fields and done
    // are protected by workerMutex_; description is immutable after dispatch.
    struct Work
    {
        GraphicsPipeline::CreateInfo description;
        std::atomic_bool cancelled{false};
        std::shared_ptr<const GraphicsPipeline> result;
        std::exception_ptr error;
        bool done = false;
    };
    struct Job
    {
        GraphicsPipelineKey key;
        GraphicsPipeline::CreateInfo description;
        render::PipelinePreparationState state = render::PipelinePreparationState::Queued;
        std::string error;
        std::size_t subscribers = 0;
        std::shared_ptr<Work> work;
    };
    struct Request
    {
        render::PipelinePreparationStatus status;
        std::vector<std::shared_ptr<Job>> jobs;
    };
    void runWorker() noexcept;
    void eraseJob(const std::shared_ptr<Job> &job);
    void checkThread() const;
    void finish(Request &request);
    void update(Request &request);
    const Device &device_;
    GraphicsPipelineCache &cache_;
    std::size_t maxRequests_;
    std::size_t pipelinesPerAdvance_;
    std::thread::id thread_;
    uint64_t nextTicket_ = 1;
    std::map<uint64_t, Request> requests_;
    std::unordered_map<GraphicsPipelineKey, std::shared_ptr<Job>, GraphicsPipelineKeyHash> jobs_;
    std::deque<std::shared_ptr<Job>> queue_;
    // Also bounds worker-queued/running/completed-but-unpublished inputs to the budget.
    std::vector<std::shared_ptr<Job>> active_;
    std::mutex workerMutex_;
    std::condition_variable workerWake_;
    bool stopping_ = false;
    std::deque<std::shared_ptr<Work>> workerQueue_;
    std::thread worker_;
};
} // namespace rubia::rhi::vulkan
