#include "vulkan/VulkanUploadService.hpp"
#include "vulkan/TextureVkFormat.hpp"

#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>
#include <type_traits>

namespace rubia::rhi::vulkan
{
namespace
{
const UploadBytes& source(const UploadOperation& op)
{
    return std::visit([](const auto& value) -> const UploadBytes& { return value.source; }, op);
}
const void* target(const UploadOperation& op)
{
    return std::visit([](const auto& value) -> const void* { return value.destination.get(); }, op);
}
UploadContext::BufferUploadInfo info(const BufferUpload& op)
{
    return {op.destination.get(), op.destinationOffset, op.source.data,
            op.source.size,       op.finalStages,       op.finalAccess};
}
UploadContext::ImageUploadInfo info(const ImageUpload& op)
{
    const auto& desc = op.destination->description();
    UploadContext::ImageUploadInfo out;
    out.destination = {op.destination->get(), desc.format, desc.extent, desc.mipLevels,
                       desc.arrayLayers};
    out.sourceData = op.source.data;
    out.sourceSize = op.source.size;
    out.copyRegions = op.regions;
    out.destinationRange = op.range;
    out.finalLayout = op.finalLayout;
    out.finalStageMask = op.finalStages;
    out.finalAccessMask = op.finalAccess;
    return out;
}
void validate(const BufferUpload& op)
{
    UploadContext::validateBufferUpload(info(op));
}
void validate(const ImageUpload& op)
{
    const auto& d = op.destination->description();
    if (d.type != VK_IMAGE_TYPE_2D || d.samples != VK_SAMPLE_COUNT_1_BIT ||
        d.initialLayout != VK_IMAGE_LAYOUT_UNDEFINED ||
        !(d.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) ||
        op.range.aspectMask != VK_IMAGE_ASPECT_COLOR_BIT ||
        (op.finalLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
         op.finalLayout != VK_IMAGE_LAYOUT_GENERAL))
    {
        throw std::invalid_argument("image upload requires a new single-sample 2D color image");
    }
    UploadContext::validateImageUploadInfo(info(op));
    auto mapping = textureFormatFromVk(d.format);
    if (!mapping)
    {
        throw std::invalid_argument("unsupported upload image format");
    }
    auto format = asset::textureFormatInfo(mapping->format);
    std::set<std::pair<uint32_t, uint32_t>> levels;
    for (const auto& r : op.regions)
    {
        const auto mip = r.imageSubresource.mipLevel;
        const uint32_t w = std::max(1u, d.extent.width >> std::min(mip, 31u));
        const uint32_t h = std::max(1u, d.extent.height >> std::min(mip, 31u));
        if (r.bufferRowLength || r.bufferImageHeight || r.imageOffset.x || r.imageOffset.y ||
            r.imageOffset.z || r.imageExtent.width != w || r.imageExtent.height != h ||
            r.imageExtent.depth != 1 || r.imageSubresource.layerCount != 1 || r.bufferOffset % 4 ||
            r.bufferOffset % format.bytesPerBlock ||
            !levels.emplace(mip, r.imageSubresource.baseArrayLayer).second)
        {
            throw std::invalid_argument(
                "v1 image uploads require unique, full, tightly packed mip layers");
        }
        const auto bytes = asset::textureMipByteSize(mapping->format, w, h);
        if (r.bufferOffset > op.source.size || bytes > op.source.size - r.bufferOffset)
        {
            throw std::invalid_argument("image copy exceeds source bytes");
        }
    }
    if (uint64_t(op.range.levelCount) * op.range.layerCount != levels.size())
    {
        throw std::invalid_argument("image upload must initialize every subresource in its range");
    }
}
} // namespace

VulkanUploadService::VulkanUploadService(const Device& device, UploadLimits limits)
    : device_(device), limits_(limits), thread_(std::this_thread::get_id()),
      commandPool_(device, device.graphicsQueueFamily(), VK_COMMAND_POOL_CREATE_TRANSIENT_BIT),
      uploadsContext_(device, commandPool_)
{
    if (!limits.maxStagingBytes || !limits.maxQueuedBytes || !limits.maxRequests)
    {
        throw std::invalid_argument("upload limits must be positive");
    }
}
VulkanUploadService::~VulkanUploadService()
{
    shutdown();
}
void VulkanUploadService::checkThread() const
{
    if (std::this_thread::get_id() != thread_)
    {
        throw std::logic_error("upload service must be used on its owning render thread");
    }
}

UploadEnqueueResult VulkanUploadService::tryEnqueue(UploadRequest& request)
{
    checkThread();
    if (stopped_ || !serviceError_.empty())
    {
        return {UploadEnqueueCode::ServiceFailed,
                {},
                "upload service is stopped or failed: " + serviceError_};
    }
    uint64_t bytes = 0;
    try
    {
        if (request.operations.empty())
        {
            throw std::invalid_argument("empty upload request");
        }
        std::set<const void*> destinations;
        for (const auto& op : request.operations)
        {
            const auto& data = source(op);
            if (!data.owner || !data.data || !data.size || data.size > limits_.maxStagingBytes ||
                data.size > limits_.maxQueuedBytes - bytes)
            {
                throw std::invalid_argument(
                    "invalid source or operation exceeds upload capacity; split it before enqueue");
            }
            std::visit(
                [&](const auto& value)
                {
                    if (!value.destination || !*value.destination ||
                        value.destination->ownerDevice() != device_.get())
                    {
                        throw std::invalid_argument("invalid upload destination or wrong device");
                    }
                    validate(value);
                },
                op);
            if (!destinations.insert(target(op)).second)
            {
                throw std::invalid_argument("v1 allows one operation per target in a request");
            }
            // Do not record overlapping initializations, including cancelled in-flight work.
            for (const auto& pair : records_)
            {
                for (const auto& pending : pair.second->request.operations)
                {
                    if (target(pending) == target(op))
                    {
                        throw std::invalid_argument("upload destination already pending");
                    }
                }
            }
            for (const auto& part : batch_)
            {
                if (target(part.operation) == target(op))
                {
                    throw std::invalid_argument("upload destination still in flight");
                }
            }
            bytes += data.size;
        }
    }
    catch (const std::exception& e)
    {
        return {UploadEnqueueCode::InvalidRequest, {}, e.what()};
    }
    if (records_.size() >= limits_.maxRequests || bytes > limits_.maxQueuedBytes - queuedBytes_)
    {
        return {UploadEnqueueCode::QueueFull, {}, {}};
    }
    if (!nextTicket_)
    {
        throw std::overflow_error("upload ticket space exhausted");
    }
    const uint64_t id = nextTicket_;
    auto record = std::make_shared<Record>();
    record->status.totalBytes = bytes;
    records_.emplace(id, record);
    try
    {
        queue_.push_back(id);
    }
    catch (...)
    {
        records_.erase(id);
        throw;
    }
    record->request = std::move(request);
    queuedBytes_ += bytes;
    ++nextTicket_;
    return {UploadEnqueueCode::Accepted, {id}, {}};
}
UploadStatus VulkanUploadService::query(UploadTicket ticket) const
{
    checkThread();
    return records_.at(ticket.value)->status;
}
void VulkanUploadService::retire(const std::shared_ptr<Record>& record)
{
    if (record->accounted)
    {
        queuedBytes_ -= record->status.totalBytes;
        record->accounted = false;
    }
    record->request.operations.clear();
}
void VulkanUploadService::cancel(UploadTicket ticket)
{
    checkThread();
    auto record = records_.at(ticket.value);
    if (!uploadFinished(record->status.state))
    {
        record->status.state = UploadState::Cancelled;
        retire(record); // batch_ independently retains submitted sources/targets.
    }
    queue_.erase(std::remove(queue_.begin(), queue_.end(), ticket.value), queue_.end());
}
void VulkanUploadService::releaseTicket(UploadTicket ticket)
{
    checkThread();
    if (!uploadFinished(records_.at(ticket.value)->status.state))
    {
        throw std::logic_error("cancel or complete upload before releasing ticket");
    }
    records_.erase(ticket.value);
}
void VulkanUploadService::finishBatch()
{
    for (auto& part : batch_)
    {
        auto& r = *part.record;
        if (uploadFinished(r.status.state))
        {
            continue;
        }
        r.status.completedBytes += part.bytes;
        ++r.completed;
        if (r.completed == r.request.operations.size())
        {
            r.status.state = UploadState::Completed;
            retire(part.record);
        }
    }
    batch_.clear();
}
void VulkanUploadService::failService(const std::string& error)
{
    serviceError_ = error;
    uploadsContext_.discardBatch();
    for (auto& pair : records_)
    {
        auto& record = pair.second;
        if (!uploadFinished(record->status.state))
        {
            record->status.state = UploadState::Failed;
            record->status.error = error;
            retire(record);
        }
    }
    batch_.clear();
    queue_.clear();
}
void VulkanUploadService::tick(const UploadBudget& budget)
{
    checkThread();
    if (stopped_ || !serviceError_.empty())
    {
        return;
    }
    if (!batch_.empty())
    {
        try
        {
            if (!uploadsContext_.pollBatch())
            {
                return;
            }
        }
        catch (const std::exception& e)
        {
            failService(e.what());
            return;
        }
        finishBatch();
    }
    if (!budget.maxBytesPerTick || !budget.maxOperationsPerTick || budget.maxCpuTime.count() <= 0)
    {
        return;
    }
    const auto deadline = std::chrono::steady_clock::now() + budget.maxCpuTime;
    uint64_t bytes = 0;
    bool submitting = false;
    try
    {
        while (!queue_.empty() && batch_.size() < budget.maxOperationsPerTick)
        {
            auto id = queue_.front();
            auto it = records_.find(id);
            if (it == records_.end() || uploadFinished(it->second->status.state))
            {
                queue_.pop_front();
                continue;
            }
            auto r = it->second;
            const auto& op = r->request.operations.at(r->next);
            const auto size = source(op).size;
            if (!batch_.empty() &&
                (size > limits_.maxStagingBytes - bytes || bytes >= budget.maxBytesPerTick ||
                 std::chrono::steady_clock::now() >= deadline))
            {
                break;
            }
            // Retain before recording or submission. A failure rolls back the whole unsubmitted
            // batch.
            batch_.push_back({r, op, size});
            if (batch_.size() == 1)
            {
                uploadsContext_.beginBatch();
            }
            std::visit(
                [&](const auto& value)
                {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, BufferUpload>)
                    {
                        uploadsContext_.recordBufferUpload(info(value));
                    }
                    else
                    {
                        uploadsContext_.recordImageUpload(info(value));
                    }
                },
                op);
            ++r->next;
            if (r->next < r->request.operations.size())
            {
                queue_.push_back(id);
            }
            queue_.pop_front();
            bytes += size;
        }
        if (batch_.empty())
        {
            return;
        }
        submitting = true;
        uploadsContext_.submitBatch();
        // No allocation after successful submission.
        for (auto& part : batch_)
        {
            part.record->status.state = UploadState::Uploading;
            part.record->status.submittedBytes += part.bytes;
        }
    }
    catch (const std::exception& e)
    {
        if (submitting)
        {
            failService(e.what());
            return;
        }
        uploadsContext_.discardBatch();
        for (auto& part : batch_)
        {
            part.record->status.state = UploadState::Failed;
            part.record->status.error = e.what();
            retire(part.record);
        }
        batch_.clear();
    }
}
void VulkanUploadService::drain()
{
    checkThread();
    while (!queue_.empty() || !batch_.empty())
    {
        tick();
        if (!batch_.empty())
        {
            try
            {
                uploadsContext_.waitBatch();
                finishBatch();
            }
            catch (const std::exception& e)
            {
                failService(e.what());
            }
        }
        if (!serviceError_.empty())
        {
            break;
        }
    }
}
void VulkanUploadService::shutdown() noexcept
{
    if (stopped_)
    {
        return;
    }
    stopped_ = true;
    // Explicit destruction path may block. Keep all destinations alive until drained.
    uploadsContext_.discardBatch();
    for (auto& pair : records_)
    {
        if (!uploadFinished(pair.second->status.state))
        {
            pair.second->status.state = UploadState::Cancelled;
        }
        retire(pair.second);
    }
    batch_.clear();
    queue_.clear();
}
} // namespace rubia::rhi::vulkan
