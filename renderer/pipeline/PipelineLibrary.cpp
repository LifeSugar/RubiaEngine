#include "pipeline/PipelineLibrary.hpp"
#include <stdexcept>

namespace rubia::render
{
PipelineLibrary::PipelineLibrary(const rhi::PipelineDevice &device)
    : device_(device), deviceHandle_(device.deviceId()), thread_(std::this_thread::get_id())
{
    if (!device.deviceId())
        throw std::invalid_argument("pipeline cache requires a live device");
}
void PipelineLibrary::checkThread() const
{
    if (std::this_thread::get_id() != thread_)
        throw std::logic_error("pipeline cache used from another thread");
    if (device_.deviceId() != deviceHandle_)
        throw std::logic_error("pipeline cache device was replaced");
}
rhi::PipelineRef PipelineLibrary::getOrCreate(
    const rhi::GraphicsPipelineDesc &info)
{
    checkThread();
    device_.validateGraphicsPipeline(info);
    auto key = PipelineKey::from(info);
    ++statistics_.requests;
    const auto found = pipelines_.find(key);
    if (found != pipelines_.end())
    {
        ++statistics_.hits;
        return found->second;
    }
    auto pipeline = device_.createGraphicsPipeline(info);
    // Failure never publishes an entry, so corrected requests can retry.
    pipelines_.emplace(std::move(key), pipeline);
    ++statistics_.creations;
    return pipeline;
}
rhi::PipelineRef PipelineLibrary::find(
    const rhi::GraphicsPipelineDesc &info) const
{
    checkThread();
    device_.validateGraphicsPipeline(info);
    const auto found = pipelines_.find(PipelineKey::from(info));
    return found == pipelines_.end() ? nullptr : found->second;
}
rhi::PipelineRef PipelineLibrary::publish(
    const PipelineKey &key, rhi::PipelineRef pipeline)
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
void PipelineLibrary::clear()
{
    checkThread();
    pipelines_.clear();
}
} // namespace rubia::render
