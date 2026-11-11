#pragma once
#include "pipeline/PipelineKey.hpp"
#include <thread>
#include <unordered_map>

namespace rubia::render
{
/// Device-scoped render-thread application cache, not a driver VkPipelineCache.
/// Use on its creating thread. Device must outlive this cache and all results.
class PipelineLibrary final
{
  public:
    struct Statistics
    {
        size_t requests = 0;
        size_t hits = 0;
        size_t creations = 0; // Published unique PSOs; discarded worker results do not count.
    };
    explicit PipelineLibrary(const rhi::PipelineDevice &device);
    PipelineLibrary(const PipelineLibrary &) = delete;
    PipelineLibrary &operator=(const PipelineLibrary &) = delete;
    rhi::PipelineRef getOrCreate(const rhi::GraphicsPipelineDesc &info);
    rhi::PipelineRef find(const rhi::GraphicsPipelineDesc &info) const;
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
    friend class PipelinePreparation;
    // Only the scheduler can publish: key and result originate from the same snapshot.
    rhi::PipelineRef publish(
        const PipelineKey &key, rhi::PipelineRef pipeline);
    void checkThread() const;
    const rhi::PipelineDevice &device_;
    rhi::DeviceId deviceHandle_ = 0;
    std::thread::id thread_;
    Statistics statistics_;
    std::unordered_map<PipelineKey, rhi::PipelineRef,
                       PipelineKeyHash>
        pipelines_;
};
} // namespace rubia::render
