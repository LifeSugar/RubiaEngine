#include "vulkan/SwapchainResources.hpp"

#include "vulkan/Device.hpp"

#include <array>
#include <stdexcept>
#include <utility>

namespace rubia::rhi::vulkan
{

SwapchainResources::SwapchainResources(
    const Device& device,
    const CreateInfo& createInfo)
{
    create(device, createInfo);
}

SwapchainResources::~SwapchainResources()
{
    reset();
}

SwapchainResources::SwapchainResources(SwapchainResources&& other) noexcept
    : swapchain_(std::move(other.swapchain_)),
      renderPass_(std::move(other.renderPass_)),
      images_(std::move(other.images_)),
      depthFormat_(std::exchange(other.depthFormat_, VK_FORMAT_UNDEFINED))
{
}

SwapchainResources& SwapchainResources::operator=(SwapchainResources&& other) noexcept
{
    if (this != &other)
    {
        reset();
        swapchain_ = std::move(other.swapchain_);
        renderPass_ = std::move(other.renderPass_);
        images_ = std::move(other.images_);
        depthFormat_ = std::exchange(other.depthFormat_, VK_FORMAT_UNDEFINED);
    }
    return *this;
}

void SwapchainResources::create(
    const Device& device,
    const CreateInfo& createInfo)
{
    if (!device ||
        createInfo.surface == VK_NULL_HANDLE ||
        createInfo.framebufferExtent.width == 0 ||
        createInfo.framebufferExtent.height == 0)
    {
        throw std::invalid_argument("swapchain resources create info is incomplete");
    }

    SwapchainResources replacement;
    replacement.swapchain_.create(
        device.physical(),
        device.get(),
        createInfo.surface,
        device.graphicsQueueFamily(),
        device.presentQueueFamily(),
        createInfo.framebufferExtent, VK_NULL_HANDLE, createInfo.presentation);
    replacement.depthFormat_ = findDepthFormat(device);
    replacement.renderPass_ = makeRenderPass(
        device,
        replacement.swapchain_.format(),
        replacement.depthFormat_);

    replacement.images_ = makeImageResources(
        device,
        replacement.swapchain_,
        replacement.depthFormat_,
        replacement.renderPass_.get());

    *this = std::move(replacement);
}

bool SwapchainResources::recreate(
    const Device& device,
    const CreateInfo& createInfo)
{
    if (!*this)
    {
        create(device, createInfo);
        return true;
    }
    if (!device ||
        createInfo.surface == VK_NULL_HANDLE ||
        createInfo.framebufferExtent.width == 0 ||
        createInfo.framebufferExtent.height == 0)
    {
        throw std::invalid_argument("swapchain resources recreate info is incomplete");
    }

    Swapchain newSwapchain(
        device.physical(),
        device.get(),
        createInfo.surface,
        device.graphicsQueueFamily(),
        device.presentQueueFamily(),
        createInfo.framebufferExtent,
        swapchain_.get(), createInfo.presentation);
    const VkFormat newDepthFormat = findDepthFormat(device);
    RenderPass newRenderPass = makeRenderPass(
        device,
        newSwapchain.format(),
        newDepthFormat);

    const bool renderPassCompatibilityChanged =
        swapchain_.format() != newSwapchain.format() ||
        depthFormat_ != newDepthFormat;

    std::vector<ImageResources> newImages = makeImageResources(
        device,
        newSwapchain,
        newDepthFormat,
        newRenderPass.get());

    // Commit only after every replacement resource has been created.
    images_.clear();
    renderPass_.reset();
    swapchain_ = std::move(newSwapchain);
    renderPass_ = std::move(newRenderPass);
    images_ = std::move(newImages);
    depthFormat_ = newDepthFormat;
    return renderPassCompatibilityChanged;
}

void SwapchainResources::reset() noexcept
{
    images_.clear();
    renderPass_.reset();
    swapchain_.reset();
    depthFormat_ = VK_FORMAT_UNDEFINED;
}

VkResult SwapchainResources::acquireNextImage(
    const Device& device,
    VkSemaphore imageAvailable,
    uint32_t& imageIndex) const
{
    if (!*this || !device || imageAvailable == VK_NULL_HANDLE)
    {
        throw std::logic_error("cannot acquire an image from incomplete swapchain resources");
    }

    return vkAcquireNextImageKHR(
        device.get(),
        swapchain_.get(),
        UINT64_MAX,
        imageAvailable,
        VK_NULL_HANDLE,
        &imageIndex);
}

VkResult SwapchainResources::present(VkQueue presentQueue, uint32_t imageIndex) const
{
    if (presentQueue == VK_NULL_HANDLE)
    {
        throw std::invalid_argument("cannot present with an invalid queue");
    }

    const VkSemaphore waitSemaphore = renderFinished(imageIndex);
    const VkSwapchainKHR swapchainHandle = swapchain_.get();

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &waitSemaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchainHandle;
    presentInfo.pImageIndices = &imageIndex;

    return vkQueuePresentKHR(presentQueue, &presentInfo);
}

void SwapchainResources::waitUntilImageReusable(
    const Device& device,
    uint32_t imageIndex) const
{
    const VkFence fence = image(imageIndex).imageInFlight;
    if (fence != VK_NULL_HANDLE &&
        vkWaitForFences(device.get(), 1, &fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to wait for swapchain image fence!");
    }
}

void SwapchainResources::markImageInFlight(uint32_t imageIndex, VkFence fence)
{
    image(imageIndex).imageInFlight = fence;
}

VkFramebuffer SwapchainResources::framebuffer(uint32_t imageIndex) const
{
    return image(imageIndex).framebuffer.get();
}

VkSemaphore SwapchainResources::renderFinished(uint32_t imageIndex) const
{
    return image(imageIndex).renderFinished.get();
}

VkFormat SwapchainResources::findDepthFormat(const Device& device)
{
    return device.findDepthStencilFormat();
}

RenderPass SwapchainResources::makeRenderPass(
    const Device& device,
    VkFormat colorFormat,
    VkFormat depthFormat)
{
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = colorFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = depthFormat;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout =
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorReference{};
    colorReference.attachment = 0;
    colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthReference{};
    depthReference.attachment = 1;
    depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorReference;
    subpass.pDepthStencilAttachment = &depthReference;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    const std::array<VkAttachmentDescription, 2> attachments = {
        colorAttachment,
        depthAttachment
    };

    VkRenderPassCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    createInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    createInfo.pAttachments = attachments.data();
    createInfo.subpassCount = 1;
    createInfo.pSubpasses = &subpass;
    createInfo.dependencyCount = 1;
    createInfo.pDependencies = &dependency;

    return RenderPass(device.get(), createInfo);
}

std::vector<SwapchainResources::ImageResources>
SwapchainResources::makeImageResources(
    const Device& device,
    const Swapchain& swapchain,
    VkFormat depthFormat,
    VkRenderPass renderPass)
{
    const std::vector<ImageView>& imageViews = swapchain.imageViews();
    std::vector<ImageResources> resources;
    resources.reserve(imageViews.size());

    for (const ImageView& colorView : imageViews)
    {
        ImageResources imageResources;
        Image::CreateInfo imageInfo{};
        imageInfo.extent = {
            swapchain.extent().width,
            swapchain.extent().height,
            1};
        imageInfo.format = depthFormat;
        imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        imageInfo.memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        imageResources.depthImage.create(device, imageInfo);

        ImageView::CreateInfo viewInfo{};
        viewInfo.image = imageResources.depthImage.get();
        viewInfo.format = depthFormat;
        viewInfo.subresourceRange.aspectMask =
            VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        imageResources.depthImageView.create(device.get(), viewInfo);

        const std::vector<VkImageView> attachments = {
            colorView.get(),
            imageResources.depthImageView.get()
        };
        imageResources.framebuffer.create(
            device.get(),
            renderPass,
            attachments,
            swapchain.extent());
        imageResources.renderFinished.create(device.get());
        resources.push_back(std::move(imageResources));
    }

    return resources;
}

const SwapchainResources::ImageResources&
SwapchainResources::image(uint32_t imageIndex) const
{
    if (imageIndex >= images_.size())
    {
        throw std::out_of_range("swapchain image index is out of range");
    }
    return images_[imageIndex];
}

SwapchainResources::ImageResources&
SwapchainResources::image(uint32_t imageIndex)
{
    if (imageIndex >= images_.size())
    {
        throw std::out_of_range("swapchain image index is out of range");
    }
    return images_[imageIndex];
}

} // namespace rubia::rhi::vulkan
