#include "vulkan/VulkanRenderer.hpp"
#include "vulkan/VulkanContext.hpp"
#include "vulkan/Buffer.hpp"
#include <cstring>
#include <stdexcept>

namespace rubia::rhi::vulkan
{
VulkanRenderer::ViewportCapture VulkanRenderer::captureEditorViewport(uint32_t frameIndex)
{
    if (!sceneReady() || outputMode_ != OutputMode::Editor || frameIndex >= editorViewportTargets_.size())
        throw std::invalid_argument("Viewport capture requires a rendered editor frame slot");
    waitIdle();
    const auto& target = editorViewportTargets_.at(frameIndex);
    const auto& device = context_->device();
    ViewportCapture result{target.extent().width, target.extent().height, {}};
    result.rgba.resize(static_cast<size_t>(result.width) * result.height * 4);
    Buffer host(device, result.rgba.size(), VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    CommandPool pool(device, device.graphicsQueueFamily());
    const auto cb = pool.allocatePrimary();
    const auto check = [](VkResult code) {
        if (code != VK_SUCCESS) throw std::runtime_error("Viewport capture command failed");
    };
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(cb, &begin));
    VkImageMemoryBarrier image{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    image.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
    image.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    image.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    image.srcQueueFamilyIndex = image.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    image.image = target.image(0);
    image.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &image);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {result.width, result.height, 1};
    vkCmdCopyImageToBuffer(cb, image.image, image.newLayout, host.get(), 1, &region);
    image.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    image.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    std::swap(image.oldLayout, image.newLayout);
    VkBufferMemoryBarrier buffer{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    buffer.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    buffer.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    buffer.srcQueueFamilyIndex = buffer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    buffer.buffer = host.get(); buffer.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 1, &buffer, 1, &image);
    check(vkEndCommandBuffer(cb));
    Fence fence(device.get());
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1; submit.pCommandBuffers = &cb;
    check(vkQueueSubmit(device.graphicsQueue(), 1, &submit, fence.get()));
    fence.wait();
    std::memcpy(result.rgba.data(), host.map(), result.rgba.size());
    if (editorViewportFormat_ == VK_FORMAT_B8G8R8A8_UNORM)
        for (size_t i = 0; i < result.rgba.size(); i += 4) std::swap(result.rgba[i], result.rgba[i+2]);
    return result;
}
}
