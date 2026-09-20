#include "vulkan/UploadContext.hpp"
#include "vulkan/UploadValidation.hpp"

#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

namespace rubia::rhi::vulkan
{
UploadContext::UploadContext(const Device& device, CommandPool& commandPool)
    : device_(&device),
      commandPool_(&commandPool)
{
    if (!device || !commandPool)
    {
        throw std::invalid_argument("cannot create an UploadContext with an invalid device or command pool");
    }
}

UploadContext::~UploadContext()
{
    discardBatch();
}

void UploadContext::beginBatch()
{
    if (commandBuffer_ != VK_NULL_HANDLE)
    {
        throw std::logic_error("previous upload batch has not been completed");
    }
    commandBuffer_ = commandPool_->allocatePrimary();
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(commandBuffer_, &beginInfo) != VK_SUCCESS)
    {
        discardBatch();
        throw std::runtime_error("failed to begin upload batch");
    }
}

void UploadContext::submitBatch()
{
    if (commandBuffer_ == VK_NULL_HANDLE || submitted_)
    {
        throw std::logic_error("no upload batch is recording");
    }
    if (vkEndCommandBuffer(commandBuffer_) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to record upload batch");
    }
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (vkCreateFence(device_->get(), &fenceInfo, nullptr, &fence_) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to create upload fence");
    }
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer_;
    if (vkQueueSubmit(device_->graphicsQueue(), 1, &submitInfo, fence_) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to submit upload batch");
    }
    submitted_ = true;
}
//轮询
bool UploadContext::pollBatch()
{
    if (!submitted_)
    {
        return commandBuffer_ == VK_NULL_HANDLE;
    }
    const VkResult result = vkGetFenceStatus(device_->get(), fence_);
    if (result == VK_NOT_READY)
    {
        return false;
    }
    if (result != VK_SUCCESS)
    {
        throw std::runtime_error("failed to query upload fence");
    }
    submitted_ = false;
    discardBatch();
    return true;
}

void UploadContext::waitBatch()
{
    if (submitted_ && vkWaitForFences(device_->get(), 1, &fence_, VK_TRUE,
            std::numeric_limits<uint64_t>::max()) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to wait for upload fence");
    }
    submitted_ = false;
    discardBatch();
}

void UploadContext::discardBatch() noexcept
{
    if (submitted_)
    {
        vkWaitForFences(device_->get(), 1, &fence_, VK_TRUE,
            std::numeric_limits<uint64_t>::max());
    }
    submitted_ = false;
    if (commandBuffer_ != VK_NULL_HANDLE)
    {
        commandPool_->free(commandBuffer_);
        commandBuffer_ = VK_NULL_HANDLE;
    }
    if (fence_ != VK_NULL_HANDLE)
    {
        vkDestroyFence(device_->get(), fence_, nullptr);
        fence_ = VK_NULL_HANDLE;
    }
    stagingBuffers_.clear();
    stagedBytes_ = 0;
}

void UploadContext::recordBufferUpload(const BufferUpload& info)
{
    validateBufferUpload(info);
    if (commandBuffer_ == VK_NULL_HANDLE || submitted_ ||
        info.destination->ownerDevice() != device_->get())
    {
        throw std::logic_error("buffer recording requires an open upload batch");
    }
    Buffer staging(*device_, info.source.size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    std::memcpy(staging.map(), info.source.data, static_cast<std::size_t>(info.source.size));
    staging.unmap();
    stagingBuffers_.push_back(std::move(staging));
    stagedBytes_ += info.source.size;
    VkBufferCopy copy{};
    copy.dstOffset = info.destinationOffset;
    copy.size = info.source.size;
    vkCmdCopyBuffer(commandBuffer_, stagingBuffers_.back().get(), info.destination->get(), 1,
                    &copy);
    VkBufferMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = info.finalAccess;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = info.destination->get();
    barrier.offset = info.destinationOffset;
    barrier.size = info.source.size;
    vkCmdPipelineBarrier(commandBuffer_, VK_PIPELINE_STAGE_TRANSFER_BIT, info.finalStages, 0, 0,
                         nullptr, 1, &barrier, 0, nullptr);
}

void UploadContext::recordImageUpload(const ImageUpload& uploadInfo)
{
    validateImageUpload(uploadInfo);
    if (submitted_ || commandBuffer_ == VK_NULL_HANDLE ||
        uploadInfo.destination->ownerDevice() != device_->get())
    {
        throw std::logic_error("an upload batch is still in flight");
    }
    Buffer stagingBuffer(*device_, uploadInfo.source.size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    std::memcpy(stagingBuffer.map(), uploadInfo.source.data,
                static_cast<std::size_t>(uploadInfo.source.size));
    stagingBuffer.unmap();
    stagingBuffers_.push_back(std::move(stagingBuffer));
    stagedBytes_ += uploadInfo.source.size;
    VkImageMemoryBarrier toTransfer{};
    toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toTransfer.oldLayout = uploadInfo.before.layout;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image = uploadInfo.destination->get();
    toTransfer.subresourceRange = uploadInfo.range;
    toTransfer.srcAccessMask = uploadInfo.before.access;
    toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(commandBuffer_, uploadInfo.before.stages, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &toTransfer);

    vkCmdCopyBufferToImage(commandBuffer_, stagingBuffers_.back().get(),
                           uploadInfo.destination->get(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           static_cast<uint32_t>(uploadInfo.regions.size()),
                           uploadInfo.regions.data());

    VkImageMemoryBarrier toFinal = toTransfer;
    toFinal.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toFinal.newLayout = uploadInfo.after.layout;
    toFinal.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toFinal.dstAccessMask = uploadInfo.after.access;
    vkCmdPipelineBarrier(commandBuffer_, VK_PIPELINE_STAGE_TRANSFER_BIT, uploadInfo.after.stages, 0,
                         0, nullptr, 0, nullptr, 1, &toFinal);
}

} // namespace rubia::rhi::vulkan
