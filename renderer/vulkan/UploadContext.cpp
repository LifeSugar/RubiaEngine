#include "vulkan/UploadContext.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

namespace rubia::rhi::vulkan
{
namespace
{

bool rangeFits(
    uint32_t base,
    uint32_t count,
    uint32_t available) noexcept
{
    return count != 0 && base <= available && count <= available - base;
}

uint32_t mipDimension(uint32_t base, uint32_t mipLevel) noexcept
{
    return std::max(1u, base >> std::min(mipLevel, 31u));
}

bool rangeContains(
    uint32_t outerBase,
    uint32_t outerCount,
    uint32_t innerBase,
    uint32_t innerCount) noexcept
{
    if (innerCount == 0 || innerBase < outerBase)
    {
        return false;
    }
    const uint32_t relativeBase = innerBase - outerBase;
    return relativeBase <= outerCount &&
        innerCount <= outerCount - relativeBase;
}

void validateUploadDescription(
    const UploadContext::ImageUploadInfo& uploadInfo)
{
    if (uploadInfo.sourceData == nullptr ||
        uploadInfo.sourceSize == 0)
    {
        throw std::invalid_argument(
            "image upload source data must not be empty");
    }
    if (uploadInfo.copyRegions.empty() ||
        uploadInfo.copyRegions.size() >
            std::numeric_limits<uint32_t>::max())
    {
        throw std::invalid_argument(
            "image upload must contain a valid number of copy regions");
    }
    if (uploadInfo.destination.image == VK_NULL_HANDLE ||
        uploadInfo.destination.format == VK_FORMAT_UNDEFINED ||
        uploadInfo.destination.extent.width == 0 ||
        uploadInfo.destination.extent.height == 0 ||
        uploadInfo.destination.extent.depth == 0 ||
        uploadInfo.destination.mipLevels == 0 ||
        uploadInfo.destination.arrayLayers == 0)
    {
        throw std::invalid_argument(
            "image upload destination is incomplete");
    }
    if (uploadInfo.sourceStageMask == 0 ||
        uploadInfo.finalLayout == VK_IMAGE_LAYOUT_UNDEFINED ||
        uploadInfo.finalStageMask == 0)
    {
        throw std::invalid_argument(
            "image upload final synchronization state is invalid");
    }
}

void validateDestinationRange(
    const UploadContext::ImageUploadInfo& uploadInfo)
{
    const VkImageSubresourceRange& range = uploadInfo.destinationRange;
    if (range.aspectMask == 0 ||
        !rangeFits(
            range.baseMipLevel,
            range.levelCount,
            uploadInfo.destination.mipLevels) ||
        !rangeFits(
            range.baseArrayLayer,
            range.layerCount,
            uploadInfo.destination.arrayLayers))
    {
        throw std::invalid_argument(
            "image upload destination range is invalid");
    }
}

void validateCopySubresource(
    const VkImageSubresourceLayers& subresource,
    const VkImageSubresourceRange& destinationRange)
{
    if (subresource.aspectMask == 0 ||
        (subresource.aspectMask & destinationRange.aspectMask) !=
            subresource.aspectMask)
    {
        throw std::invalid_argument(
            "image upload copy aspect is outside the destination range");
    }
    if (!rangeContains(
            destinationRange.baseMipLevel,
            destinationRange.levelCount,
            subresource.mipLevel,
            1))
    {
        throw std::invalid_argument(
            "image upload copy mip is outside the destination range");
    }
    if (!rangeContains(
            destinationRange.baseArrayLayer,
            destinationRange.layerCount,
            subresource.baseArrayLayer,
            subresource.layerCount))
    {
        throw std::invalid_argument(
            "image upload copy layers are outside the destination range");
    }
}

void validateCopyExtent(
    const VkBufferImageCopy& region,
    const UploadContext::ImageDestination& destination)
{
    if (region.imageOffset.x < 0 ||
        region.imageOffset.y < 0 ||
        region.imageOffset.z < 0 ||
        region.imageExtent.width == 0 ||
        region.imageExtent.height == 0 ||
        region.imageExtent.depth == 0)
    {
        throw std::invalid_argument(
            "image upload copy offset or extent is invalid");
    }

    const uint32_t mipLevel = region.imageSubresource.mipLevel;
    const VkExtent3D mipExtent{
        mipDimension(destination.extent.width, mipLevel),
        mipDimension(destination.extent.height, mipLevel),
        mipDimension(destination.extent.depth, mipLevel)};
    if (static_cast<uint64_t>(region.imageOffset.x) +
            region.imageExtent.width > mipExtent.width ||
        static_cast<uint64_t>(region.imageOffset.y) +
            region.imageExtent.height > mipExtent.height ||
        static_cast<uint64_t>(region.imageOffset.z) +
            region.imageExtent.depth > mipExtent.depth)
    {
        throw std::invalid_argument(
            "image upload copy region exceeds its destination mip");
    }
}

void validateCopyRegion(
    const VkBufferImageCopy& region,
    const UploadContext::ImageUploadInfo& uploadInfo)
{
    if (region.bufferOffset >= uploadInfo.sourceSize)
    {
        throw std::invalid_argument(
            "image upload copy offset is outside the source buffer");
    }
    validateCopySubresource(
        region.imageSubresource,
        uploadInfo.destinationRange);
    validateCopyExtent(region, uploadInfo.destination);
}

void validateImageUpload(const UploadContext::ImageUploadInfo& uploadInfo)
{
    validateUploadDescription(uploadInfo);
    validateDestinationRange(uploadInfo);
    for (const VkBufferImageCopy& region : uploadInfo.copyRegions)
    {
        validateCopyRegion(region, uploadInfo);
    }
}

} // namespace

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

void UploadContext::validateBufferUpload(const BufferUploadInfo& info)
{
    if (!info.destination || !*info.destination || !info.sourceData || !info.sourceSize ||
        info.sourceSize > std::numeric_limits<std::size_t>::max() ||
        info.destinationOffset > info.destination->size() ||
        info.sourceSize > info.destination->size() - info.destinationOffset ||
        (info.destinationOffset % 4) != 0 || (info.sourceSize % 4) != 0 || !info.finalStages ||
        !info.finalAccess || !(info.destination->usage() & VK_BUFFER_USAGE_TRANSFER_DST_BIT))
    {
        throw std::invalid_argument("invalid buffer upload range, usage or synchronization");
    }
}

void UploadContext::validateImageUploadInfo(const ImageUploadInfo& info)
{
    validateImageUpload(info);
}

void UploadContext::recordBufferUpload(const BufferUploadInfo& info)
{
    validateBufferUpload(info);
    if (commandBuffer_ == VK_NULL_HANDLE || submitted_)
    {
        throw std::logic_error("buffer recording requires an open upload batch");
    }
    Buffer staging(*device_, info.sourceSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    std::memcpy(staging.map(), info.sourceData, static_cast<std::size_t>(info.sourceSize));
    staging.unmap();
    stagingBuffers_.push_back(std::move(staging));
    stagedBytes_ += info.sourceSize;
    VkBufferCopy copy{};
    copy.dstOffset = info.destinationOffset;
    copy.size = info.sourceSize;
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
    barrier.size = info.sourceSize;
    vkCmdPipelineBarrier(commandBuffer_, VK_PIPELINE_STAGE_TRANSFER_BIT, info.finalStages, 0, 0,
                         nullptr, 1, &barrier, 0, nullptr);
}

Buffer UploadContext::uploadBuffer(const void* data, VkDeviceSize size,
                                   VkBufferUsageFlags destinationUsage)
{
    Buffer destination(*device_, size, destinationUsage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    const bool synchronous = commandBuffer_ == VK_NULL_HANDLE;
    try
    {
        if (synchronous)
        {
            beginBatch();
        }
        recordBufferUpload({&destination, 0, data, size, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                            VK_ACCESS_MEMORY_READ_BIT});
        if (synchronous)
        {
            submitBatch();
            waitBatch();
        }
    }
    catch (...)
    {
        discardBatch();
        throw;
    }
    return destination;
}

void UploadContext::uploadImage(const ImageUploadInfo& info)
{
    const bool synchronous = commandBuffer_ == VK_NULL_HANDLE;
    try
    {
        if (synchronous)
        {
            beginBatch();
        }
        recordImageUpload(info);
        if (synchronous)
        {
            submitBatch();
            waitBatch();
        }
    }
    catch (...)
    {
        discardBatch();
        throw;
    }
}

void UploadContext::recordImageUpload(const ImageUploadInfo& uploadInfo)
{
    validateImageUpload(uploadInfo);
    if (submitted_ || commandBuffer_ == VK_NULL_HANDLE)
    {
        throw std::logic_error("an upload batch is still in flight");
    }
    if (uploadInfo.sourceSize > std::numeric_limits<std::size_t>::max())
    {
        throw std::overflow_error("upload image size exceeds the host address range");
    }
    Buffer stagingBuffer(*device_, uploadInfo.sourceSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    std::memcpy(stagingBuffer.map(), uploadInfo.sourceData,
                static_cast<std::size_t>(uploadInfo.sourceSize));
    stagingBuffer.unmap();
    stagingBuffers_.push_back(std::move(stagingBuffer));
    stagedBytes_ += uploadInfo.sourceSize;
    VkImageMemoryBarrier toTransfer{};
    toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toTransfer.oldLayout = uploadInfo.oldLayout;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image = uploadInfo.destination.image;
    toTransfer.subresourceRange = uploadInfo.destinationRange;
    toTransfer.srcAccessMask = uploadInfo.sourceAccessMask;
    toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(commandBuffer_, uploadInfo.sourceStageMask, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &toTransfer);

    vkCmdCopyBufferToImage(commandBuffer_, stagingBuffers_.back().get(),
                           uploadInfo.destination.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           static_cast<uint32_t>(uploadInfo.copyRegions.size()),
                           uploadInfo.copyRegions.data());

    VkImageMemoryBarrier toFinal = toTransfer;
    toFinal.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toFinal.newLayout = uploadInfo.finalLayout;
    toFinal.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toFinal.dstAccessMask = uploadInfo.finalAccessMask;
    vkCmdPipelineBarrier(commandBuffer_, VK_PIPELINE_STAGE_TRANSFER_BIT, uploadInfo.finalStageMask,
                         0, 0, nullptr, 0, nullptr, 1, &toFinal);
}

} // namespace rubia::rhi::vulkan
