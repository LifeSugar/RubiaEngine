#include "vulkan/UploadContext.hpp"
#include "vulkan/TextureVkFormat.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

namespace rubia::rhi::vulkan
{
namespace
{
bool rangeFits(uint32_t base, uint32_t count, uint32_t available) noexcept
{
    return count != 0 && base <= available && count <= available - base;
}
void validateSource(const UploadBytes& source)
{
    if (!source.owner || !source.data || !source.size)
    {
        throw std::invalid_argument("upload requires an owned, nonempty source");
    }
}
} // namespace

void UploadContext::validateBufferUpload(const BufferUpload& op)
{
    validateSource(op.source);
    if (!op.destination || !*op.destination || op.destinationOffset > op.destination->size() ||
        op.source.size > op.destination->size() - op.destinationOffset ||
        op.destinationOffset % 4 || op.source.size % 4 || !op.finalStages || !op.finalAccess ||
        !(op.destination->usage() & VK_BUFFER_USAGE_TRANSFER_DST_BIT))
    {
        throw std::invalid_argument("invalid buffer upload range, usage or synchronization");
    }
}

void UploadContext::validateImageUpload(const ImageUpload& op)
{
    validateSource(op.source);
    if (!op.destination || !*op.destination)
    {
        throw std::invalid_argument("image upload requires an existing destination");
    }
    const auto& d = op.destination->description();
    if (d.type != VK_IMAGE_TYPE_2D || d.samples != VK_SAMPLE_COUNT_1_BIT ||
        d.initialLayout != VK_IMAGE_LAYOUT_UNDEFINED ||
        !(d.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) ||
        op.range.aspectMask != VK_IMAGE_ASPECT_COLOR_BIT ||
        !rangeFits(op.range.baseMipLevel, op.range.levelCount, d.mipLevels) ||
        !rangeFits(op.range.baseArrayLayer, op.range.layerCount, d.arrayLayers) ||
        !op.finalStages || !op.finalAccess ||
        (op.finalLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
         op.finalLayout != VK_IMAGE_LAYOUT_GENERAL) ||
        (op.finalLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
         !(d.usage & (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT))))
    {
        throw std::invalid_argument(
            "image upload requires a new 2D color image and valid range/use");
    }
    if (op.regions.empty() || op.regions.size() > std::numeric_limits<uint32_t>::max())
    {
        throw std::invalid_argument("invalid image upload region count");
    }
    const auto mapping = textureFormatFromVk(d.format);
    if (!mapping)
    {
        throw std::invalid_argument("unsupported upload image format");
    }
    const auto format = asset::textureFormatInfo(mapping->format);
    std::set<std::pair<uint32_t, uint32_t>> levels;
    for (const auto& r : op.regions)
    {
        const auto mip = r.imageSubresource.mipLevel;
        const auto layer = r.imageSubresource.baseArrayLayer;
        if (r.imageSubresource.aspectMask != op.range.aspectMask || mip < op.range.baseMipLevel ||
            mip - op.range.baseMipLevel >= op.range.levelCount || layer < op.range.baseArrayLayer ||
            layer - op.range.baseArrayLayer >= op.range.layerCount)
        {
            throw std::invalid_argument("image copy is outside the upload subresource range");
        }
        const uint32_t w = std::max(1u, d.extent.width >> std::min(mip, 31u));
        const uint32_t h = std::max(1u, d.extent.height >> std::min(mip, 31u));
        if (r.bufferRowLength || r.bufferImageHeight || r.imageOffset.x || r.imageOffset.y ||
            r.imageOffset.z || r.imageExtent.width != w || r.imageExtent.height != h ||
            r.imageExtent.depth != 1 || r.imageSubresource.layerCount != 1 || r.bufferOffset % 4 ||
            r.bufferOffset % format.bytesPerBlock || !levels.emplace(mip, layer).second)
        {
            throw std::invalid_argument(
                "image upload requires unique, full, tightly packed mip layers");
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
    toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image = uploadInfo.destination->get();
    toTransfer.subresourceRange = uploadInfo.range;
    toTransfer.srcAccessMask = 0;
    toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(commandBuffer_, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toTransfer);

    vkCmdCopyBufferToImage(commandBuffer_, stagingBuffers_.back().get(),
                           uploadInfo.destination->get(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           static_cast<uint32_t>(uploadInfo.regions.size()),
                           uploadInfo.regions.data());

    VkImageMemoryBarrier toFinal = toTransfer;
    toFinal.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toFinal.newLayout = uploadInfo.finalLayout;
    toFinal.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toFinal.dstAccessMask = uploadInfo.finalAccess;
    vkCmdPipelineBarrier(commandBuffer_, VK_PIPELINE_STAGE_TRANSFER_BIT, uploadInfo.finalStages, 0,
                         0, nullptr, 0, nullptr, 1, &toFinal);
}

} // namespace rubia::rhi::vulkan
