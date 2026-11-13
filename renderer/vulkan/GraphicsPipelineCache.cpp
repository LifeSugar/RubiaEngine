#include "vulkan/GraphicsPipelineCache.hpp"
#include "vulkan/Device.hpp"
#include <stdexcept>

namespace rubia::rhi::vulkan
{
GraphicsPipelineCache::GraphicsPipelineCache(const Device &device)
    : device_(device), deviceHandle_(device.get()), thread_(std::this_thread::get_id())
{
    if (!device)
        throw std::invalid_argument("pipeline cache requires a live device");
}
void GraphicsPipelineCache::checkThread() const
{
    if (std::this_thread::get_id() != thread_)
        throw std::logic_error("pipeline cache used from another thread");
    if (device_.get() != deviceHandle_)
        throw std::logic_error("pipeline cache device was replaced");
}
std::shared_ptr<const GraphicsPipeline> GraphicsPipelineCache::getOrCreate(
    const GraphicsPipeline::CreateInfo &info)
{
    checkThread();
    GraphicsPipeline::validate(device_, info);
    auto key = GraphicsPipelineKey::from(info);
    ++statistics_.requests;
    const auto found = pipelines_.find(key);
    if (found != pipelines_.end())
    {
        ++statistics_.hits;
        return found->second;
    }
    auto pipeline = std::make_shared<GraphicsPipeline>(device_, info);
    // Failure never publishes an entry, so corrected requests can retry.
    pipelines_.emplace(std::move(key), pipeline);
    ++statistics_.creations;
    return pipeline;
}
std::shared_ptr<const GraphicsPipeline> GraphicsPipelineCache::find(
    const GraphicsPipeline::CreateInfo &info) const
{
    checkThread();
    GraphicsPipeline::validate(device_, info);
    const auto found = pipelines_.find(GraphicsPipelineKey::from(info));
    return found == pipelines_.end() ? nullptr : found->second;
}
std::shared_ptr<const GraphicsPipeline> GraphicsPipelineCache::publish(
    const GraphicsPipelineKey &key, std::shared_ptr<const GraphicsPipeline> pipeline)
{
    checkThread();
    if (!pipeline)
        throw std::invalid_argument("cannot publish an empty pipeline");
    ++statistics_.requests;
    const auto [entry, inserted] = pipelines_.emplace(key, std::move(pipeline));
    if (inserted)
        ++statistics_.creations;
    else
        ++statistics_.hits; // A draw fallback won while the worker was creating.
    return entry->second;
}
void GraphicsPipelineCache::clear()
{
    checkThread();
    pipelines_.clear();
}
} // namespace rubia::rhi::vulkan
