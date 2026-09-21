#pragma once

#include "vulkan/Framebuffer.hpp"
#include "vulkan/Image.hpp"
#include "vulkan/ImageView.hpp"
#include "vulkan/RenderPass.hpp"
#include "vulkan/Semaphore.hpp"
#include "vulkan/Swapchain.hpp"

#include <vulkan/vulkan.h>

#include <cstddef>
#include <vector>

namespace rubia::rhi::vulkan
{

class Device;

// Owns resources whose lifetime follows the swapchain or one of its images.
// Window-system interaction stays outside; callers provide the framebuffer size.
class SwapchainResources final
{
public:
    /// Parameters used to build swapchain-dependent resources.
    struct CreateInfo
    {
        /// Presentation surface targeted by the swapchain.
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        /// Current drawable size requested for the swapchain.
        VkExtent2D framebufferExtent{};
    };

    /// Creates an empty swapchain-resource set.
    SwapchainResources() = default;
    /// Creates all resources required by a swapchain.
    SwapchainResources(const Device& device, const CreateInfo& createInfo);
    /// Releases all swapchain-dependent resources.
    ~SwapchainResources();

    SwapchainResources(const SwapchainResources&) = delete;
    SwapchainResources& operator=(const SwapchainResources&) = delete;
    /// Transfers all swapchain-dependent resources from another wrapper.
    SwapchainResources(SwapchainResources&& other) noexcept;
    /// Replaces this set by taking resources from another wrapper.
    SwapchainResources& operator=(SwapchainResources&& other) noexcept;

    /// Creates or replaces the complete swapchain-resource set.
    void create(const Device& device, const CreateInfo& createInfo);
    // Returns true when pipelines created for the old render pass are no longer
    // compatible and must be rebuilt by the renderer/pipeline cache.
    [[nodiscard]] bool recreate(const Device& device, const CreateInfo& createInfo);
    /// Releases the swapchain and every dependent resource.
    void reset() noexcept;

    /// Acquires the next presentable image and signals the supplied semaphore.
    [[nodiscard]] VkResult acquireNextImage(
        const Device& device,
        VkSemaphore imageAvailable,
        uint32_t& imageIndex) const;
    /// Queues the selected image for presentation after rendering completes.
    [[nodiscard]] VkResult present(VkQueue presentQueue, uint32_t imageIndex) const;

    /// Waits until the selected image is no longer used by an earlier frame.
    void waitUntilImageReusable(const Device& device, uint32_t imageIndex) const;
    /// Associates the selected image with its current in-flight fence.
    void markImageInFlight(uint32_t imageIndex, VkFence fence);

    /// Returns the owned Vulkan swapchain handle.
    [[nodiscard]] VkSwapchainKHR get() const noexcept { return swapchain_.get(); }
    /// Returns the swapchain color format.
    [[nodiscard]] VkFormat format() const noexcept { return swapchain_.format(); }
    /// Returns the swapchain presentation color space.
    [[nodiscard]] VkColorSpaceKHR colorSpace() const noexcept
    {
        return swapchain_.colorSpace();
    }
    /// Returns the swapchain image extent.
    [[nodiscard]] VkExtent2D extent() const noexcept { return swapchain_.extent(); }
    /// Returns the number of swapchain image-resource bundles.
    [[nodiscard]] std::size_t imageCount() const noexcept { return images_.size(); }
    /// Returns the render pass shared by all swapchain images.
    [[nodiscard]] VkRenderPass renderPass() const noexcept { return renderPass_.get(); }
    std::shared_ptr<const RenderPassState> renderPassReference() const noexcept { return renderPass_.reference(); }
    /// Returns the framebuffer associated with one swapchain image.
    [[nodiscard]] VkFramebuffer framebuffer(uint32_t imageIndex) const;
    /// Returns the render-complete semaphore for one swapchain image.
    [[nodiscard]] VkSemaphore renderFinished(uint32_t imageIndex) const;
    /// Returns whether the swapchain-resource set is initialized.
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return static_cast<bool>(swapchain_);
    }

private:
    /// Resources owned for one swapchain image.
    struct ImageResources
    {
        /// Depth attachment image matching the swapchain extent.
        Image depthImage;
        /// View used to bind the depth image as an attachment.
        ImageView depthImageView;
        /// Framebuffer combining this image's color and depth views.
        Framebuffer framebuffer;
        /// Semaphore signaled when rendering to this image completes.
        Semaphore renderFinished;

        // Non-owning fence of the FrameContext currently using this image.
        VkFence imageInFlight = VK_NULL_HANDLE;
    };

    /// Selects a supported depth-stencil attachment format.
    [[nodiscard]] static VkFormat findDepthFormat(const Device& device);
    /// Builds the render pass used by swapchain framebuffers.
    [[nodiscard]] static RenderPass makeRenderPass(
        const Device& device,
        VkFormat colorFormat,
        VkFormat depthFormat);
    /// Creates depth, framebuffer, and synchronization resources per image.
    [[nodiscard]] static std::vector<ImageResources> makeImageResources(
        const Device& device,
        const Swapchain& swapchain,
        VkFormat depthFormat,
        VkRenderPass renderPass);

    /// Returns checked read-only access to one image-resource bundle.
    [[nodiscard]] const ImageResources& image(uint32_t imageIndex) const;
    /// Returns checked mutable access to one image-resource bundle.
    [[nodiscard]] ImageResources& image(uint32_t imageIndex);

    // Declaration order encodes destruction dependencies:
    // images -> render pass -> swapchain.
    /// Owned presentation swapchain and its color-image views.
    Swapchain swapchain_;
    /// Render pass shared by every swapchain framebuffer.
    RenderPass renderPass_;
    /// Per-image depth, framebuffer, and synchronization resources.
    std::vector<ImageResources> images_;
    /// Depth-stencil format used by the current image resources.
    VkFormat depthFormat_ = VK_FORMAT_UNDEFINED;
};

} // namespace rubia::rhi::vulkan
