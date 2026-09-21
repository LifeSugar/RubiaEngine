#pragma once
#include "vulkan/GraphicsPipelineKey.hpp"
#include <thread>
#include <unordered_map>

namespace rubia::rhi::vulkan
{
/// Device-scoped render-thread application cache, not a driver VkPipelineCache.
/// Use on its creating thread. Device must outlive this cache and all results.
class GraphicsPipelineCache final
{
  public:
    struct Statistics
    {
        size_t requests = 0;
        size_t hits = 0;
        size_t creations = 0; // Published unique PSOs; discarded worker results do not count.
    };
    explicit GraphicsPipelineCache(const Device &device);
    GraphicsPipelineCache(const GraphicsPipelineCache &) = delete;
    GraphicsPipelineCache &operator=(const GraphicsPipelineCache &) = delete;
    std::shared_ptr<const GraphicsPipeline> getOrCreate(const GraphicsPipeline::CreateInfo &info);
    std::shared_ptr<const GraphicsPipeline> find(const GraphicsPipeline::CreateInfo &info) const;
    /// Drops cache ownership only. In-flight users must retain their result owners.
    void clear();
    size_t size() const
    {
        checkThread();
        return pipelines_.size();
    }
    Statistics statistics() const
    {
        checkThread();
        return statistics_;
    }

  private:
    friend class VulkanPipelinePreparation;
    // Only the scheduler can publish: key and result originate from the same snapshot.
    std::shared_ptr<const GraphicsPipeline> publish(
        const GraphicsPipelineKey &key, std::shared_ptr<const GraphicsPipeline> pipeline);
    void checkThread() const;
    const Device &device_;
    VkDevice deviceHandle_ = VK_NULL_HANDLE;
    std::thread::id thread_;
    Statistics statistics_;
    std::unordered_map<GraphicsPipelineKey, std::shared_ptr<const GraphicsPipeline>,
                       GraphicsPipelineKeyHash>
        pipelines_;
};
} // namespace rubia::rhi::vulkan
