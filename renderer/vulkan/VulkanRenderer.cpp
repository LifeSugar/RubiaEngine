#include "vulkan/VulkanRenderer.hpp"

#include "vulkan/GpuMaterial.hpp"
#include "vulkan/Mesh.hpp"
#include "vulkan/RenderAssetCache.hpp"
#include "vulkan/VulkanDrawListCompiler.hpp"
#include "vulkan/VulkanContext.hpp"
#include "vulkan/VulkanScenePreparation.hpp"
#include "vulkan/GpuTexture.hpp"

#include <imgui_impl_vulkan.h>

#include <array>
#include <stdexcept>
#include <utility>

namespace rubia::rhi::vulkan
{

namespace
{

constexpr std::size_t kSceneColorAttachment = 0;
constexpr std::size_t kSceneDepthAttachment = 1;

RenderPass makeSceneRenderPass(
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
    // The integration step will add an explicit transition from attachment
    // writes to shader sampling between the scene and present passes.
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = depthFormat;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout =
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorReference{};
    colorReference.attachment =
        static_cast<uint32_t>(kSceneColorAttachment);
    colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthReference{};
    depthReference.attachment =
        static_cast<uint32_t>(kSceneDepthAttachment);
    depthReference.layout =
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

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
    createInfo.attachmentCount =
        static_cast<uint32_t>(attachments.size());
    createInfo.pAttachments = attachments.data();
    createInfo.subpassCount = 1;
    createInfo.pSubpasses = &subpass;
    createInfo.dependencyCount = 1;
    createInfo.pDependencies = &dependency;

    return RenderPass(device.get(), createInfo);
}

std::vector<RenderTarget> makeSceneRenderTargets(
    const Device& device,
    VkRenderPass renderPass,
    VkExtent2D extent,
    uint32_t frameCount,
    VkFormat colorFormat,
    VkFormat depthFormat)
{
    RenderTarget::AttachmentInfo colorAttachment{};
    colorAttachment.format = colorFormat;
    colorAttachment.usage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT;
    colorAttachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

    RenderTarget::AttachmentInfo depthAttachment{};
    depthAttachment.format = depthFormat;
    depthAttachment.usage =
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    depthAttachment.aspectMask =
        VK_IMAGE_ASPECT_DEPTH_BIT |
        VK_IMAGE_ASPECT_STENCIL_BIT;

    RenderTarget::CreateInfo createInfo{};
    createInfo.renderPass = renderPass;
    createInfo.extent = extent;
    createInfo.attachments = {
        colorAttachment,
        depthAttachment
    };

    std::vector<RenderTarget> targets;
    targets.reserve(frameCount);
    for (uint32_t i = 0; i < frameCount; ++i)
    {
        targets.emplace_back(device, createInfo);
    }
    return targets;
}

RenderPass makeEditorViewportRenderPass(
    const Device& device,
    VkFormat colorFormat)
{
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = colorFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout =
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference colorReference{};
    colorReference.attachment = 0;
    colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorReference;

    std::array<VkSubpassDependency, 2> dependencies{};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[0].dstStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    createInfo.attachmentCount = 1;
    createInfo.pAttachments = &colorAttachment;
    createInfo.subpassCount = 1;
    createInfo.pSubpasses = &subpass;
    createInfo.dependencyCount =
        static_cast<uint32_t>(dependencies.size());
    createInfo.pDependencies = dependencies.data();
    return RenderPass(device.get(), createInfo);
}

std::vector<RenderTarget> makeEditorViewportTargets(
    const Device& device,
    VkRenderPass renderPass,
    VkExtent2D extent,
    uint32_t frameCount,
    VkFormat colorFormat)
{
    RenderTarget::AttachmentInfo colorAttachment{};
    colorAttachment.format = colorFormat;
    colorAttachment.usage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT;
    colorAttachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

    RenderTarget::CreateInfo createInfo{};
    createInfo.renderPass = renderPass;
    createInfo.extent = extent;
    createInfo.attachments = {colorAttachment};

    std::vector<RenderTarget> targets;
    targets.reserve(frameCount);
    for (uint32_t index = 0; index < frameCount; ++index)
    {
        targets.emplace_back(device, createInfo);
    }
    return targets;
}

bool isSrgbFormat(VkFormat format) noexcept
{
    switch (format)
    {
    case VK_FORMAT_R8G8B8_SRGB:
    case VK_FORMAT_R8G8B8A8_SRGB:
    case VK_FORMAT_B8G8R8_SRGB:
    case VK_FORMAT_B8G8R8A8_SRGB:
    case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
        return true;
    default:
        return false;
    }
}

uint32_t selectPresentOutputTransferFunction(
    VkFormat format,
    VkColorSpaceKHR colorSpace)
{
    if (isSrgbFormat(format))
    {
        // The color attachment performs linear-to-sRGB encoding.
        return 0;
    }
    if (colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
    {
        // A UNORM attachment needs explicit encoding in the shader.
        return 1;
    }
    throw std::runtime_error(
        "present shader does not support the selected output color space");
}

VkSamplerCreateInfo makePresentSamplerCreateInfo()
{
    VkSamplerCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    createInfo.magFilter = VK_FILTER_LINEAR;
    createInfo.minFilter = VK_FILTER_LINEAR;
    createInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    createInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    createInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    createInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    createInfo.minLod = 0.0f;
    createInfo.maxLod = 0.0f;
    return createInfo;
}

void updatePresentDescriptorSets(
    VkDevice device,
    const std::vector<RenderTarget>& sceneRenderTargets,
    VkSampler sampler,
    const std::vector<VkDescriptorSet>& descriptorSets)
{
    if (sceneRenderTargets.size() != descriptorSets.size())
    {
        throw std::invalid_argument(
            "present descriptor count must match scene render targets");
    }

    for (std::size_t i = 0; i < descriptorSets.size(); ++i)
    {
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageView =
            sceneRenderTargets[i].imageView(kSceneColorAttachment);
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDescriptorImageInfo samplerInfo{};
        samplerInfo.sampler = sampler;

        std::array<VkWriteDescriptorSet, 2> writes{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = descriptorSets[i];
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        writes[0].pImageInfo = &imageInfo;
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = descriptorSets[i];
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        writes[1].pImageInfo = &samplerInfo;

        vkUpdateDescriptorSets(
            device,
            static_cast<uint32_t>(writes.size()),
            writes.data(),
            0,
            nullptr);
    }
}

} // namespace

VulkanRenderer::VulkanRenderer() = default;

VulkanRenderer::VulkanRenderer(const CreateInfo& createInfo)
{
    create(createInfo);
}

VulkanRenderer::~VulkanRenderer()
{
    reset();
}

void VulkanRenderer::create(const CreateInfo& createInfo)
{
    createPresentation(createInfo);
    try
    {
        createSceneResources(createInfo.graphicsPipeline,
            createInfo.presentPipeline, createInfo.maxRenderObjects);
    }
    catch (...)
    {
        reset();
        throw;
    }
}

void VulkanRenderer::createPresentation(const CreateInfo& createInfo)
{
    if (createInfo.context == nullptr || !*createInfo.context)
    {
        throw std::invalid_argument(
            "cannot create VulkanRenderer with an invalid VulkanContext");
    }
    if (createInfo.framebufferExtent.width == 0 ||
        createInfo.framebufferExtent.height == 0)
    {
        throw std::invalid_argument(
            "cannot create VulkanRenderer with an empty framebuffer extent");
    }
    if (createInfo.framesInFlight == 0)
    {
        throw std::invalid_argument(
            "VulkanRenderer requires at least one frame in flight");
    }
    if (createInfo.maxRenderObjects == 0)
    {
        throw std::invalid_argument(
            "VulkanRenderer requires a non-zero render object capacity");
    }

    reset();
    context_ = createInfo.context;
    outputMode_ = createInfo.outputMode;

    try
    {
        const Device& device = context_->device();
        SwapchainResources::CreateInfo swapchainCreateInfo{};
        swapchainCreateInfo.surface = context_->surface();
        swapchainCreateInfo.framebufferExtent =
            createInfo.framebufferExtent;
        swapchainResources_.create(device, swapchainCreateInfo);

        createFrameContexts(createInfo.framesInFlight);
        uploads_ = std::make_unique<VulkanUploadService>(device);
    }
    catch (...)
    {
        reset();
        throw;
    }
}

void VulkanRenderer::beginScenePreparation(
    RenderAssetCache& renderAssets, render::SceneResourceRequest request)
{
    if (!*this || hasSceneResources() ||
        (scenePreparation_ && scenePreparation_->ownsResources()) ||
        !renderAssets.empty() || !pendingTextures_.empty())
    {
        throw std::logic_error("scene preparation requires presentation and an empty scene/cache");
    }
    scenePreparation_ = std::make_unique<VulkanScenePreparation>(
        context_->device(), *this, renderAssets, *uploads_, std::move(request));
    scenePreparation_->begin();
}

void VulkanRenderer::advanceScenePreparation()
{
    advanceResourcePreparation();
}

void VulkanRenderer::advanceResourcePreparation()
{
    if (!uploads_)
    {
        return;
    }
    if (scenePreparation_)
    {
        scenePreparation_->advance();
    }
    uploads_->tick();
    for (auto& pair : pendingTextures_)
    {
        auto& pending = pair.second;
        if (pending.published || pending.cancelled || !pending.error.empty())
        {
            continue;
        }
        const auto status = uploads_->query({pair.first});
        if (status.state == UploadState::Failed || status.state == UploadState::Cancelled)
        {
            pending.texture.reset();
            continue;
        }
        if (status.state == UploadState::Completed)
        {
            try
            {
                pending.cache->publishTexture(pending.handle, std::move(*pending.texture));
                pending.published = true;
                pending.texture.reset();
            }
            catch (const std::exception& error)
            {
                pending.error = error.what();
                pending.texture.reset();
            }
        }
    }
}

UploadEnqueueResult VulkanRenderer::prepareTexture(
    RenderAssetCache& cache, asset::TextureAssetHandle handle,
    std::shared_ptr<const asset::TextureAsset> source)
{
    if (!uploads_ || !handle || !source || !*source || cache.tryTexture(handle) ||
        (scenePreparation_ && scenePreparation_->ownsResources()))
    {
        return {UploadEnqueueCode::InvalidRequest,
                {},
                "texture preparation requires a new asset slot and no scene preparation"};
    }
    for (const auto& pair : pendingTextures_)
    {
        if (pair.second.cache == &cache && pair.second.handle.index == handle.index)
        {
            return {UploadEnqueueCode::InvalidRequest,
                    {},
                    "texture slot already has a preparation ticket"};
        }
    }
    auto texture = std::make_shared<GpuTexture>();
    GpuTexture::CreateInfo info;
    info.asset = source.get();
    texture->allocate(context_->device(), info);
    auto request = GpuTexture::makeUploadRequest(texture, std::move(source));
    auto result = uploads_->tryEnqueue(request);
    if (result.accepted())
    {
        try
        {
            pendingTextures_.emplace(result.ticket.value,
                                     PendingTexturePreparation{&cache, handle, texture});
        }
        catch (...)
        {
            uploads_->cancel(result.ticket);
            uploads_->releaseTicket(result.ticket);
            throw;
        }
    }
    return result;
}

GpuTexture VulkanRenderer::uploadTextureAndWait(std::shared_ptr<const asset::TextureAsset> source)
{
    if (!uploads_ || !source || !*source ||
        (scenePreparation_ && scenePreparation_->ownsResources()))
    {
        throw std::logic_error(
            "blocking texture upload requires valid assets and no scene preparation");
    }
    auto texture = std::make_shared<GpuTexture>();
    GpuTexture::CreateInfo info;
    info.asset = source.get();
    texture->allocate(context_->device(), info);
    auto request = GpuTexture::makeUploadRequest(texture, std::move(source));
    auto result = uploads_->tryEnqueue(request);
    if (result.code == UploadEnqueueCode::QueueFull)
    {
        uploads_->drain();
        result = uploads_->tryEnqueue(request);
    }
    if (!result.accepted())
    {
        throw std::runtime_error(result.error.empty() ? "texture upload queue is full"
                                                      : result.error);
    }
    try
    {
        uploads_->drain();
        const auto status = uploads_->query(result.ticket);
        if (status.state != UploadState::Completed)
        {
            throw std::runtime_error("texture upload failed: " + status.error);
        }
    }
    catch (...)
    {
        uploads_->cancel(result.ticket);
        uploads_->releaseTicket(result.ticket);
        throw;
    }
    uploads_->releaseTicket(result.ticket);
    return std::move(*texture);
}

UploadStatus VulkanRenderer::texturePreparationStatus(UploadTicket ticket) const
{
    const auto& pending = pendingTextures_.at(ticket.value);
    auto status = uploads_->query(ticket);
    if (pending.cancelled)
    {
        status.state = UploadState::Cancelled;
    }
    else if (!pending.error.empty())
    {
        status.state = UploadState::Failed;
        status.error = pending.error;
    }
    else if (status.state == UploadState::Completed && !pending.published)
    {
        status.state = UploadState::Uploading;
    }
    return status;
}
void VulkanRenderer::cancelTexturePreparation(UploadTicket ticket)
{
    auto& pending = pendingTextures_.at(ticket.value);
    if (uploadFinished(texturePreparationStatus(ticket).state))
    {
        return;
    }
    uploads_->cancel(ticket);
    // A blocking drain may have completed the upload without publishing it.
    // Service completion is immutable; preparation cancellation is tracked here.
    pending.cancelled = true;
    pending.texture.reset();
}
void VulkanRenderer::releaseTexturePreparation(UploadTicket ticket)
{
    // A completed upload must first be published (or explicitly failed).
    if (!uploadFinished(texturePreparationStatus(ticket).state))
    {
        throw std::logic_error("texture preparation is not finished");
    }
    uploads_->releaseTicket(ticket);
    pendingTextures_.erase(ticket.value);
}

render::ScenePreparationStatus VulkanRenderer::scenePreparationStatus() const
{
    return scenePreparation_ ? scenePreparation_->status() : render::ScenePreparationStatus{};
}

void VulkanRenderer::activatePreparedScene()
{
    if (!scenePreparation_)
    {
        throw std::logic_error("no scene preparation to activate");
    }
    scenePreparation_->activate();
}

void VulkanRenderer::cancelScenePreparation() noexcept
{
    if (scenePreparation_)
    {
        scenePreparation_->cancel();
    }
}

void VulkanRenderer::createSceneResources(
    const GraphicsPipeline::CreateInfo& graphicsPipeline,
    const GraphicsPipeline::CreateInfo& presentPipeline,
    uint32_t maxRenderObjects)
{
    if (!*this || hasSceneResources() || maxRenderObjects == 0)
    {
        throw std::logic_error("scene initialization requires presentation and no active scene");
    }
    pipelineCreateInfo_ = graphicsPipeline;
    presentPipelineCreateInfo_ = presentPipeline;
    try
    {
        const Device& device = context_->device();
        const uint32_t count = frameCount();
        frameDataResources_.create(device, count, maxRenderObjects);
        createSceneRenderTargets(extent(), count);
        if (outputMode_ == OutputMode::Editor)
        {
            createEditorViewportResources(extent(), count);
        }
        createPresentResources(count);
        graphicsPipeline_.create(device, makePipelineCreateInfo());
        presentPipeline_.create(device, makePresentPipelineCreateInfo());
    }
    catch (...)
    {
        releaseSceneResources();
        throw;
    }
}

void VulkanRenderer::resetSceneResources() noexcept
{
    if (scenePreparation_ && scenePreparation_->ownsResources())
    {
        scenePreparation_->cancel();
        return;
    }
    releaseSceneResources();
}

void VulkanRenderer::releaseSceneResources() noexcept
{
    if (context_ != nullptr)
    {
        context_->waitIdle();
    }
    presentPipeline_.reset();
    graphicsPipeline_.reset();
    presentDescriptorSets_.clear();
    presentDescriptorPool_.reset();
    presentDescriptorSetLayout_.reset();
    presentSampler_.reset();
    editorViewportTargets_.clear();
    editorViewportRenderPass_.reset();
    sceneRenderTargets_.clear();
    sceneRenderPass_.reset();
    frameDataResources_.reset();
    pipelineCreateInfo_ = {};
    presentPipelineCreateInfo_ = {};
    sceneColorFormat_ = VK_FORMAT_UNDEFINED;
    sceneDepthFormat_ = VK_FORMAT_UNDEFINED;
    editorViewportFormat_ = VK_FORMAT_UNDEFINED;
    ++editorViewportRevision_;
    presentOutputTransferFunction_ = 0;
    stagedViewId_ = {};
    stagedViewGpuDataRevision_ = 0;
}

void VulkanRenderer::reset() noexcept
{
    resetSceneResources();
    scenePreparation_.reset();
    if (uploads_) uploads_->shutdown();
    pendingTextures_.clear();
    uploads_.reset();
    frameContexts_.clear();
    swapchainResources_.reset();
    outputMode_ = OutputMode::Runtime;
    context_ = nullptr;
    currentFrame_ = 0;
}

void VulkanRenderer::waitIdle() const
{
    if (context_ != nullptr)
    {
        context_->waitIdle();
    }
}

void VulkanRenderer::resize(VkExtent2D framebufferExtent)
{
    if (!*this)
    {
        throw std::logic_error("cannot resize an uninitialized VulkanRenderer");
    }
    if (framebufferExtent.width == 0 || framebufferExtent.height == 0)
    {
        throw std::invalid_argument(
            "cannot resize VulkanRenderer to an empty framebuffer extent");
    }

    const Device& device = context_->device();
    device.waitIdle();

    // Recorded commands reference the old framebuffers and render pass.
    for (FrameContext& frame : frameContexts_)
    {
        frame.resetCommands();
    }

    VkExtent2D renderExtent = framebufferExtent;
    if (outputMode_ == OutputMode::Editor &&
        !editorViewportTargets_.empty())
    {
        renderExtent = editorViewportTargets_.front().extent();
    }

    SwapchainResources::CreateInfo createInfo{};
    createInfo.surface = context_->surface();
    createInfo.framebufferExtent = framebufferExtent;
    const bool pipelineCompatibilityChanged =
        swapchainResources_.recreate(device, createInfo);
    if (!hasSceneResources())
    {
        return;
    }

    std::vector<RenderTarget> newSceneRenderTargets =
        makeSceneRenderTargets(
            device,
            sceneRenderPass_.get(),
            outputMode_ == OutputMode::Editor
                ? renderExtent
                : swapchainResources_.extent(),
            static_cast<uint32_t>(frameContexts_.size()),
            sceneColorFormat_,
            sceneDepthFormat_);
    std::vector<RenderTarget> newEditorViewportTargets;
    if (outputMode_ == OutputMode::Editor)
    {
        newEditorViewportTargets = makeEditorViewportTargets(
            device,
            editorViewportRenderPass_.get(),
            renderExtent,
            static_cast<uint32_t>(frameContexts_.size()),
            editorViewportFormat_);
    }
    sceneRenderTargets_ = std::move(newSceneRenderTargets);
    if (outputMode_ == OutputMode::Editor)
    {
        editorViewportTargets_ = std::move(newEditorViewportTargets);
        ++editorViewportRevision_;
    }
    recreatePresentDescriptorSets(
        static_cast<uint32_t>(frameContexts_.size()));
    presentOutputTransferFunction_ =
        selectPresentOutputTransferFunction(
            swapchainResources_.format(),
            swapchainResources_.colorSpace());

    if (outputMode_ == OutputMode::Runtime &&
        pipelineCompatibilityChanged)
    {
        presentPipeline_.create(device, makePresentPipelineCreateInfo());
    }
}

void VulkanRenderer::resizeEditorViewport(VkExtent2D extent)
{
    if (!*this)
    {
        throw std::logic_error(
            "cannot resize an uninitialized VulkanRenderer");
    }
    if (outputMode_ != OutputMode::Editor)
    {
        throw std::logic_error(
            "cannot resize an Editor viewport in Runtime output mode");
    }
    if (extent.width == 0 || extent.height == 0)
    {
        throw std::invalid_argument(
            "cannot resize the Editor viewport to an empty extent");
    }

    if (!hasSceneResources())
    {
        return;
    }
    const VkExtent2D currentExtent = editorViewportTargets_.front().extent();
    if (currentExtent.width == extent.width &&
        currentExtent.height == extent.height)
    {
        return;
    }

    const Device& device = context_->device();
    device.waitIdle();

    std::vector<RenderTarget> newSceneRenderTargets =
        makeSceneRenderTargets(
            device,
            sceneRenderPass_.get(),
            extent,
            static_cast<uint32_t>(frameContexts_.size()),
            sceneColorFormat_,
            sceneDepthFormat_);
    std::vector<RenderTarget> newEditorViewportTargets =
        makeEditorViewportTargets(
            device,
            editorViewportRenderPass_.get(),
            extent,
            static_cast<uint32_t>(frameContexts_.size()),
            editorViewportFormat_);
    const uint32_t frameCount =
        static_cast<uint32_t>(frameContexts_.size());
    const std::vector<VkDescriptorPoolSize> poolSizes = {
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, frameCount},
        {VK_DESCRIPTOR_TYPE_SAMPLER, frameCount}
    };
    DescriptorPool newPresentDescriptorPool(
        device.get(),
        poolSizes,
        frameCount);
    std::vector<VkDescriptorSet> newPresentDescriptorSets =
        newPresentDescriptorPool.allocate(
            presentDescriptorSetLayout_.get(),
            frameCount);
    updatePresentDescriptorSets(
        device.get(),
        newSceneRenderTargets,
        presentSampler_.get(),
        newPresentDescriptorSets);

    // Recorded commands and presentation descriptors reference the previous
    // scene targets.
    for (FrameContext& frame : frameContexts_)
    {
        frame.resetCommands();
    }
    sceneRenderTargets_ = std::move(newSceneRenderTargets);
    editorViewportTargets_ = std::move(newEditorViewportTargets);
    presentDescriptorSets_.clear();
    presentDescriptorPool_ = std::move(newPresentDescriptorPool);
    presentDescriptorSets_ = std::move(newPresentDescriptorSets);
    ++editorViewportRevision_;
}


VulkanRenderer::RenderResult VulkanRenderer::render(
    const render::RenderFrame& frameData,
    const RenderAssetCache& renderAssets,
    ImDrawData* uiDrawData)
{
    if (!sceneReady())
    {
        throw std::logic_error("scene resources are not ready");
    }
    return renderFrame(&frameData, &renderAssets, uiDrawData);
}

VulkanRenderer::RenderResult VulkanRenderer::renderGui(ImDrawData* uiDrawData)
{
    return renderFrame(nullptr, nullptr, uiDrawData);
}

VulkanRenderer::RenderResult VulkanRenderer::renderFrame(
    const render::RenderFrame* sceneFrame,
    const RenderAssetCache* renderAssets,
    ImDrawData* uiDrawData)
{
    if (!*this)
    {
        throw std::logic_error("cannot render with an uninitialized VulkanRenderer");
    }
    VulkanDrawList drawList;
    if (sceneFrame != nullptr)
    {
        const render::RenderFrame& frameData = *sceneFrame;
        if (frameData.renderList.size() >
            frameDataResources_.objectCapacity())
        {
            throw std::invalid_argument(
                "RenderFrame exceeds VulkanRenderer object capacity");
        }
        if (!frameData.view.id || frameData.view.gpuDataRevision == 0)
        {
            throw std::invalid_argument(
                "RenderFrame contains an invalid RenderView identity or revision");
        }

        if (frameData.renderList.objectData.size() !=
            frameData.renderList.size())
        {
            throw std::invalid_argument(
                "RenderList object-data count does not match its draw count");
        }
        drawList = VulkanDrawListCompiler{}.compile(
            frameData.renderList,
            *renderAssets);

        if (!drawList.transparent.empty())
        {
            throw std::logic_error(
                "transparent RenderList requires a transparent pipeline variant");
        }
    }

    const Device& device = context_->device();
    FrameContext& frame = frameContexts_[currentFrame_];
    frame.waitUntilReusable();

    uint32_t imageIndex = 0;
    const VkResult acquireResult = swapchainResources_.acquireNextImage(
        device,
        frame.imageAvailable(),
        imageIndex);
    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR)
    {
        return RenderResult::NeedsResize;
    }
    if (acquireResult != VK_SUCCESS &&
        acquireResult != VK_SUBOPTIMAL_KHR)
    {
        throw std::runtime_error("failed to acquire swapchain image");
    }
    bool needsResize = acquireResult == VK_SUBOPTIMAL_KHR;

    swapchainResources_.waitUntilImageReusable(device, imageIndex);
    swapchainResources_.markImageInFlight(
        imageIndex,
        frame.inFlightFence());

    if (sceneFrame != nullptr)
    {
        updateFrameData(currentFrame_, *sceneFrame);
    }
    frame.resetCommands();
    recordCommandBuffer(
        frame.commandBuffer(),
        currentFrame_,
        imageIndex,
        sceneFrame != nullptr ? frameDataResources_.descriptorSet(currentFrame_) : VK_NULL_HANDLE,
        drawList,
        uiDrawData,
        sceneFrame != nullptr);

    frame.resetFence();

    const VkSemaphore waitSemaphore = frame.imageAvailable();
    const VkPipelineStageFlags waitStage =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    const VkSemaphore signalSemaphore =
        swapchainResources_.renderFinished(imageIndex);
    const VkCommandBuffer commandBuffer = frame.commandBuffer();

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &waitSemaphore;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &signalSemaphore;

    if (vkQueueSubmit(
            device.graphicsQueue(),
            1,
            &submitInfo,
            frame.inFlightFence()) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to submit draw command buffer");
    }

    const VkResult presentResult =
        swapchainResources_.present(device.presentQueue(), imageIndex);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR ||
        presentResult == VK_SUBOPTIMAL_KHR)
    {
        needsResize = true;
    }
    else if (presentResult != VK_SUCCESS)
    {
        throw std::runtime_error("failed to present swapchain image");
    }

    currentFrame_ =
        (currentFrame_ + 1) % static_cast<uint32_t>(frameContexts_.size());
    return needsResize
        ? RenderResult::NeedsResize
        : RenderResult::Rendered;
}

VulkanRenderer::operator bool() const noexcept
{
    return context_ != nullptr && static_cast<bool>(swapchainResources_) &&
        !frameContexts_.empty();
}

bool VulkanRenderer::sceneReady() const noexcept
{
    return (!scenePreparation_ || !scenePreparation_->ownsResources()) && hasSceneResources();
}

bool VulkanRenderer::hasSceneResources() const noexcept
{
    const bool editorResourcesValid = outputMode_ != OutputMode::Editor ||
        (static_cast<bool>(editorViewportRenderPass_) &&
         editorViewportTargets_.size() == frameContexts_.size() &&
         editorViewportFormat_ != VK_FORMAT_UNDEFINED &&
         editorViewportRevision_ != 0);
    return context_ != nullptr &&
        frameDataResources_.frameCount() != 0 &&
        static_cast<bool>(swapchainResources_) &&
        static_cast<bool>(sceneRenderPass_) &&
        sceneRenderTargets_.size() == frameContexts_.size() &&
        static_cast<bool>(presentSampler_) &&
        static_cast<bool>(presentDescriptorSetLayout_) &&
        static_cast<bool>(presentDescriptorPool_) &&
        presentDescriptorSets_.size() == frameContexts_.size() &&
        static_cast<bool>(graphicsPipeline_) &&
        static_cast<bool>(presentPipeline_) &&
        !frameContexts_.empty() &&
        editorResourcesValid;
}

VulkanRenderer::EditorViewportOutput
VulkanRenderer::editorViewportOutput(uint32_t frameIndex) const
{
    if (outputMode_ != OutputMode::Editor ||
        frameIndex >= editorViewportTargets_.size())
    {
        throw std::out_of_range(
            "Editor viewport output is unavailable for this frame slot");
    }

    const RenderTarget& target = editorViewportTargets_[frameIndex];
    return {
        target.imageView(0),
        target.extent(),
        editorViewportRevision_
    };
}

void VulkanRenderer::createFrameContexts(uint32_t frameCount)
{
    const Device& device = context_->device();
    std::vector<FrameContext> newContexts;
    newContexts.reserve(frameCount);
    for (uint32_t i = 0; i < frameCount; ++i)
    {
        newContexts.emplace_back(device);
    }
    frameContexts_ = std::move(newContexts);
    currentFrame_ = 0;
}

void VulkanRenderer::createSceneRenderTargets(
    VkExtent2D extent,
    uint32_t frameCount)
{
    const Device& device = context_->device();
    const VkFormat colorFormat = device.findSupportedFormat(
        {VK_FORMAT_R16G16B16A16_SFLOAT},
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
            VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
    const VkFormat depthFormat = device.findDepthStencilFormat();
    RenderPass renderPass = makeSceneRenderPass(
        device,
        colorFormat,
        depthFormat);
    std::vector<RenderTarget> targets = makeSceneRenderTargets(
        device,
        renderPass.get(),
        extent,
        frameCount,
        colorFormat,
        depthFormat);

    // Commit only after the render pass and every target succeeded.
    sceneRenderTargets_.clear();
    sceneRenderPass_.reset();
    sceneRenderPass_ = std::move(renderPass);
    sceneRenderTargets_ = std::move(targets);
    sceneColorFormat_ = colorFormat;
    sceneDepthFormat_ = depthFormat;
}

void VulkanRenderer::createEditorViewportResources(
    VkExtent2D extent,
    uint32_t frameCount)
{
    const Device& device = context_->device();
    const VkFormat colorFormat = device.findSupportedFormat(
        {
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_FORMAT_B8G8R8A8_UNORM
        },
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
            VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
    RenderPass renderPass = makeEditorViewportRenderPass(
        device,
        colorFormat);
    std::vector<RenderTarget> targets = makeEditorViewportTargets(
        device,
        renderPass.get(),
        extent,
        frameCount,
        colorFormat);

    editorViewportTargets_.clear();
    editorViewportRenderPass_.reset();
    editorViewportRenderPass_ = std::move(renderPass);
    editorViewportTargets_ = std::move(targets);
    editorViewportFormat_ = colorFormat;
    ++editorViewportRevision_;
}

void VulkanRenderer::createPresentResources(uint32_t frameCount)
{
    const Device& device = context_->device();

    Sampler sampler(device.get(), makePresentSamplerCreateInfo());

    std::vector<VkDescriptorSetLayoutBinding> bindings(2);
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    DescriptorSetLayout descriptorSetLayout(device.get(), bindings);

    const std::vector<VkDescriptorPoolSize> poolSizes = {
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, frameCount},
        {VK_DESCRIPTOR_TYPE_SAMPLER, frameCount}
    };
    DescriptorPool descriptorPool(device.get(), poolSizes, frameCount);
    std::vector<VkDescriptorSet> descriptorSets =
        descriptorPool.allocate(descriptorSetLayout.get(), frameCount);
    updatePresentDescriptorSets(
        device.get(),
        sceneRenderTargets_,
        sampler.get(),
        descriptorSets);

    presentDescriptorSets_.clear();
    presentDescriptorPool_.reset();
    presentDescriptorSetLayout_.reset();
    presentSampler_.reset();
    presentSampler_ = std::move(sampler);
    presentDescriptorSetLayout_ = std::move(descriptorSetLayout);
    presentDescriptorPool_ = std::move(descriptorPool);
    presentDescriptorSets_ = std::move(descriptorSets);
    presentOutputTransferFunction_ =
        selectPresentOutputTransferFunction(
            swapchainResources_.format(),
            swapchainResources_.colorSpace());
}

void VulkanRenderer::recreatePresentDescriptorSets(uint32_t frameCount)
{
    const Device& device = context_->device();
    const std::vector<VkDescriptorPoolSize> poolSizes = {
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, frameCount},
        {VK_DESCRIPTOR_TYPE_SAMPLER, frameCount}
    };
    DescriptorPool descriptorPool(device.get(), poolSizes, frameCount);
    std::vector<VkDescriptorSet> descriptorSets = descriptorPool.allocate(
        presentDescriptorSetLayout_.get(),
        frameCount);
    updatePresentDescriptorSets(
        device.get(),
        sceneRenderTargets_,
        presentSampler_.get(),
        descriptorSets);

    presentDescriptorSets_.clear();
    presentDescriptorPool_ = std::move(descriptorPool);
    presentDescriptorSets_ = std::move(descriptorSets);
}

GraphicsPipeline::CreateInfo VulkanRenderer::makePipelineCreateInfo() const
{
    GraphicsPipeline::CreateInfo createInfo = pipelineCreateInfo_;
    createInfo.renderPass = sceneRenderPass_.get();
    createInfo.descriptorSetLayouts.insert(
        createInfo.descriptorSetLayouts.begin(),
        frameDataResources_.descriptorSetLayout());
    return createInfo;
}

GraphicsPipeline::CreateInfo
VulkanRenderer::makePresentPipelineCreateInfo() const
{
    GraphicsPipeline::CreateInfo createInfo = presentPipelineCreateInfo_;
    createInfo.renderPass = outputMode_ == OutputMode::Editor
        ? editorViewportRenderPass_.get()
        : swapchainResources_.renderPass();
    createInfo.descriptorSetLayouts.insert(
        createInfo.descriptorSetLayouts.begin(),
        presentDescriptorSetLayout_.get());
    return createInfo;
}

void VulkanRenderer::updateFrameData(
    uint32_t frameIndex,
    const render::RenderFrame& frame)
{
    if (frame.view.id != stagedViewId_ ||
        frame.view.gpuDataRevision != stagedViewGpuDataRevision_)
    {
        frameDataResources_.setCameraData(frame.view.gpuData);
        stagedViewId_ = frame.view.id;
        stagedViewGpuDataRevision_ = frame.view.gpuDataRevision;
    }

    if (!frame.renderList.objectData.empty())
    {
        frameDataResources_.setObjectData(
            frame.renderList.objectData.data(),
            static_cast<uint32_t>(frame.renderList.objectData.size()));
    }
    frameDataResources_.sync(frameIndex);
}

void VulkanRenderer::recordCommandBuffer(
    VkCommandBuffer commandBuffer,
    uint32_t frameIndex,
    uint32_t imageIndex,
    VkDescriptorSet descriptorSet,
    const VulkanDrawList& drawList,
    ImDrawData* uiDrawData,
    bool drawScene)
{
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to begin recording command buffer");
    }

    if (!drawScene)
    {
        recordEditorUiPass(commandBuffer, imageIndex, uiDrawData);
    }
    else
    {
        recordScenePass(
            commandBuffer,
            frameIndex,
            descriptorSet,
            drawList);
        transitionSceneColorForSampling(commandBuffer, frameIndex);
        if (outputMode_ == OutputMode::Editor)
        {
            recordEditorViewportPass(commandBuffer, frameIndex);
            recordEditorUiPass(commandBuffer, imageIndex, uiDrawData);
        }
        else
        {
            recordPresentPass(
                commandBuffer,
                frameIndex,
                imageIndex,
                uiDrawData);
        }
    }

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to record command buffer");
    }
}

void VulkanRenderer::recordScenePass(
    VkCommandBuffer commandBuffer,
    uint32_t frameIndex,
    VkDescriptorSet descriptorSet,
    const VulkanDrawList& drawList)
{
    const RenderTarget& target = sceneRenderTargets_.at(frameIndex);
    const VkExtent2D renderExtent = target.extent();

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = sceneRenderPass_.get();
    renderPassInfo.framebuffer = target.framebuffer();
    renderPassInfo.renderArea.extent = renderExtent;

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};
    renderPassInfo.clearValueCount =
        static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(
        commandBuffer,
        &renderPassInfo,
        VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        graphicsPipeline_.get());

    VkViewport viewport{};
    viewport.width = static_cast<float>(renderExtent.width);
    viewport.height = static_cast<float>(renderExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent = renderExtent;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        graphicsPipeline_.layout(),
        0,
        1,
        &descriptorSet,
        0,
        nullptr);

    const std::vector<VulkanDrawItem>& opaque = drawList.opaque;
    const Mesh* boundMesh = nullptr;
    VkDescriptorSet boundMaterialDescriptorSet = VK_NULL_HANDLE;
    for (uint32_t itemIndex = 0;
         itemIndex < static_cast<uint32_t>(opaque.size());
         ++itemIndex)
    {
        const VulkanDrawItem& item = opaque[itemIndex];
        const Mesh& mesh = *item.mesh;
        if (item.mesh != boundMesh)
        {
            mesh.bind(commandBuffer);
            boundMesh = item.mesh;
        }

        const VkDescriptorSet materialDescriptorSet =
            item.material->descriptorSet();
        if (materialDescriptorSet != boundMaterialDescriptorSet)
        {
            vkCmdBindDescriptorSets(
                commandBuffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                graphicsPipeline_.layout(),
                1,
                1,
                &materialDescriptorSet,
                0,
                nullptr);
            boundMaterialDescriptorSet = materialDescriptorSet;
        }

        render::DrawPushConstants pushConstants{};
        pushConstants.objectIndex = item.objectIndex;
        vkCmdPushConstants(
            commandBuffer,
            graphicsPipeline_.layout(),
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0,
            sizeof(pushConstants),
            &pushConstants);

        const asset::SubmeshData& submesh =
            mesh.submeshes()[item.submeshIndex];
        if (submesh.indexed())
        {
            vkCmdDrawIndexed(
                commandBuffer,
                submesh.indexCount,
                1,
                submesh.firstIndex,
                0,
                0);
        }
        else
        {
            vkCmdDraw(
                commandBuffer,
                submesh.vertexCount,
                1,
                submesh.firstVertex,
                0);
        }
    }

    vkCmdEndRenderPass(commandBuffer);
}

void VulkanRenderer::transitionSceneColorForSampling(
    VkCommandBuffer commandBuffer,
    uint32_t frameIndex)
{
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image =
        sceneRenderTargets_.at(frameIndex).image(kSceneColorAttachment);
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &barrier);
}

void VulkanRenderer::recordPresentPass(
    VkCommandBuffer commandBuffer,
    uint32_t frameIndex,
    uint32_t imageIndex,
    ImDrawData* uiDrawData)
{
    const VkExtent2D presentExtent = swapchainResources_.extent();

    // SwapchainResources still owns a temporary per-image depth attachment.
    // It is cleared for render-pass compatibility but the present pipeline has
    // depth testing and writes disabled. A later step removes this attachment.
    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = swapchainResources_.renderPass();
    renderPassInfo.framebuffer =
        swapchainResources_.framebuffer(imageIndex);
    renderPassInfo.renderArea.extent = presentExtent;
    renderPassInfo.clearValueCount =
        static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(
        commandBuffer,
        &renderPassInfo,
        VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        presentPipeline_.get());

    VkViewport viewport{};
    viewport.width = static_cast<float>(presentExtent.width);
    viewport.height = static_cast<float>(presentExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent = presentExtent;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    const VkDescriptorSet descriptorSet =
        presentDescriptorSets_.at(frameIndex);
    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        presentPipeline_.layout(),
        0,
        1,
        &descriptorSet,
        0,
        nullptr);

    render::PresentPushConstants pushConstants{};
    pushConstants.outputTransferFunction =
        presentOutputTransferFunction_;
    vkCmdPushConstants(
        commandBuffer,
        presentPipeline_.layout(),
        VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        sizeof(pushConstants),
        &pushConstants);

    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    if (uiDrawData != nullptr)
    {
        ImGui_ImplVulkan_RenderDrawData(uiDrawData, commandBuffer);
    }
    vkCmdEndRenderPass(commandBuffer);
}

void VulkanRenderer::recordEditorViewportPass(
    VkCommandBuffer commandBuffer,
    uint32_t frameIndex)
{
    const RenderTarget& target = editorViewportTargets_.at(frameIndex);
    const VkExtent2D renderExtent = target.extent();

    VkClearValue clearValue{};
    clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = editorViewportRenderPass_.get();
    renderPassInfo.framebuffer = target.framebuffer();
    renderPassInfo.renderArea.extent = renderExtent;
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearValue;

    vkCmdBeginRenderPass(
        commandBuffer,
        &renderPassInfo,
        VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        presentPipeline_.get());

    VkViewport viewport{};
    viewport.width = static_cast<float>(renderExtent.width);
    viewport.height = static_cast<float>(renderExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent = renderExtent;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    const VkDescriptorSet descriptorSet =
        presentDescriptorSets_.at(frameIndex);
    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        presentPipeline_.layout(),
        0,
        1,
        &descriptorSet,
        0,
        nullptr);

    render::PresentPushConstants pushConstants{};
    // The Editor target is UNORM. Store display-encoded SDR so stock ImGui
    // can sample and copy it without a custom tone-mapping shader.
    pushConstants.outputTransferFunction = 1;
    vkCmdPushConstants(
        commandBuffer,
        presentPipeline_.layout(),
        VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        sizeof(pushConstants),
        &pushConstants);

    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    vkCmdEndRenderPass(commandBuffer);
}

void VulkanRenderer::recordEditorUiPass(
    VkCommandBuffer commandBuffer,
    uint32_t imageIndex,
    ImDrawData* uiDrawData)
{
    const VkExtent2D presentExtent = swapchainResources_.extent();

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = swapchainResources_.renderPass();
    renderPassInfo.framebuffer =
        swapchainResources_.framebuffer(imageIndex);
    renderPassInfo.renderArea.extent = presentExtent;
    renderPassInfo.clearValueCount =
        static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(
        commandBuffer,
        &renderPassInfo,
        VK_SUBPASS_CONTENTS_INLINE);
    if (uiDrawData != nullptr)
    {
        ImGui_ImplVulkan_RenderDrawData(uiDrawData, commandBuffer);
    }
    vkCmdEndRenderPass(commandBuffer);
}

} // namespace rubia::rhi::vulkan
