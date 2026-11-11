#include "vulkan/VulkanRenderer.hpp"
#include "resources/ResourcePreparationService.hpp"

#include "resources/RenderAssetStore.hpp"
#include "core/DrawCompiler.hpp"
#include "vulkan/DefaultPipelineFactory.hpp"
#include "vulkan/VulkanCommandEncoder.hpp"
#include "vulkan/VulkanPass.hpp"
#include "core/DrawRecorder.hpp"
#include "vulkan/VulkanContext.hpp"
#include "resources/ScenePreparationSession.hpp"

#include "vulkan/VulkanGuiCommandContext.hpp"

#include <array>
#include <stdexcept>
#include <utility>

namespace rubia::rhi::vulkan
{

namespace
{

constexpr std::size_t kSceneColorAttachment = render::sceneColorAttachment;
constexpr std::size_t kSceneDepthAttachment = render::sceneDepthAttachment;
constexpr std::size_t kSceneResolveAttachment = render::sceneResolveAttachment;
constexpr auto kSceneSamples = static_cast<VkSampleCountFlagBits>(render::sceneSamples);

void validateSceneSampleCount(const Device& device, VkFormat colorFormat, VkFormat depthFormat)
{
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(device.physical(), &properties);
    const auto supported = properties.limits.framebufferColorSampleCounts &
        properties.limits.framebufferDepthSampleCounts &
        properties.limits.framebufferStencilSampleCounts;
    if (!(supported & kSceneSamples))
        throw std::runtime_error("scene rendering requires 4x MSAA color and depth/stencil support");

    const auto supportsFormat = [&](VkFormat format, VkImageUsageFlags usage) {
        VkImageFormatProperties imageProperties{};
        return vkGetPhysicalDeviceImageFormatProperties(device.physical(), format,
                   VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, usage, 0,
                   &imageProperties) == VK_SUCCESS &&
            (imageProperties.sampleCounts & kSceneSamples) != 0;
    };
    if (!supportsFormat(colorFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) ||
        !supportsFormat(depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT))
        throw std::runtime_error("scene attachment formats do not support 4x MSAA");
}

RenderPass makeSceneRenderPass(const Device& device, VkFormat color, VkFormat depth)
{
    return createRenderPass(device, render::scenePassLayout(fromVulkanFormat(color), fromVulkanFormat(depth)));
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
    colorAttachment.samples = kSceneSamples;
    colorAttachment.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    colorAttachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

    RenderTarget::AttachmentInfo depthAttachment{};
    depthAttachment.format = depthFormat;
    depthAttachment.samples = kSceneSamples;
    depthAttachment.usage =
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    depthAttachment.aspectMask =
        VK_IMAGE_ASPECT_DEPTH_BIT |
        VK_IMAGE_ASPECT_STENCIL_BIT;

    RenderTarget::AttachmentInfo resolveAttachment = colorAttachment;
    resolveAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    resolveAttachment.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;

    RenderTarget::CreateInfo createInfo{};
    createInfo.renderPass = renderPass;
    createInfo.extent = extent;
    createInfo.attachments = {
        colorAttachment,
        depthAttachment,
        resolveAttachment
    };

    std::vector<RenderTarget> targets;
    targets.reserve(frameCount);
    for (uint32_t i = 0; i < frameCount; ++i)
    {
        targets.emplace_back(device, createInfo);
    }
    return targets;
}

RenderPass makeEditorViewportRenderPass(const Device& device, VkFormat color)
{
    return createRenderPass(device, render::editorOutputPassLayout(fromVulkanFormat(color)));
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
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
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

    if (!render::validMaterialParameterMemory(createInfo.resourcePreparation.material.parameterMemory))
        throw std::invalid_argument("invalid material parameter memory policy");
    reset();
    context_ = createInfo.context;
    outputMode_ = createInfo.outputMode;

    try
    {
        const Device& device = context_->device();
        scheduler_ = std::make_unique<render::FrameScheduler>(createInfo.framesInFlight);
        resourceDevice_ = std::make_unique<VulkanResourceDevice>(device);
        SwapchainResources::CreateInfo swapchainCreateInfo{};
        swapchainCreateInfo.surface = context_->surface();
        swapchainCreateInfo.framebufferExtent =
            createInfo.framebufferExtent;
        swapchainResources_.create(device, swapchainCreateInfo);

        createFrameSlots(createInfo.framesInFlight);
        pipelineDevice_ = std::make_unique<VulkanPipelineDevice>(*resourceDevice_);
        pipelineCache_ = std::make_unique<render::PipelineLibrary>(*pipelineDevice_);
        pipelinePreparation_ = std::make_unique<render::PipelinePreparation>(*pipelineDevice_, *pipelineCache_);
        uploads_ = std::make_unique<VulkanUploadService>(device);
        resourceUploads_ = std::make_unique<VulkanUploadAdapter>(*uploads_);
        resourcePreparation_ = std::make_unique<render::ResourcePreparationService>(
            *resourceDevice_, *resourceUploads_, scheduler_->pending(), createInfo.resourcePreparation);
    }
    catch (...)
    {
        reset();
        throw;
    }
}

rhi::PipelineRef VulkanRenderer::preparePipeline(const GraphicsPipeline::CreateInfo& info)
{
    if (!pipelineCache_) throw std::logic_error("renderer pipeline cache is not initialized");
    return pipelineCache_->getOrCreate(describeGraphicsPipeline(info, *resourceDevice_));
}

render::PipelineLibrary::Statistics VulkanRenderer::pipelineCacheStatistics() const
{
    return pipelineCache_ ? pipelineCache_->statistics() : render::PipelineLibrary::Statistics{};
}

render::PipelinePreparationResult VulkanRenderer::prewarmScenePipelines(
    const render::RenderAssetStore& resources, const std::vector<asset::MaterialAssetHandle>& materials)
{
    if (!pipelinePreparation_ || !hasSceneResources())
        return {ResourcePreparationCode::InvalidRequest, {}, "pipeline prewarming requires a scene pass"};
    try
    {
        const auto& pass = scenePipelineDescription_;
        std::vector<rhi::GraphicsPipelineDesc> descriptions;
        descriptions.reserve(materials.size());
        for (auto handle : materials)
        {
            const auto& material = resources.material(handle);
            const auto prepared = resources.materialTemplateForMaterial(handle);
            if (!prepared) throw std::invalid_argument("material has no resident template");
            descriptions.push_back(render::DrawCompiler::describeMaterialPipeline(*prepared,
                render::makePipelineVariantKey(material.materialTemplate(), material.renderState()), pass));
        }
        return pipelinePreparation_->request(std::move(descriptions));
    }
    catch (const std::exception& error)
    {
        return {ResourcePreparationCode::InvalidRequest, {}, error.what()};
    }
}
render::PipelinePreparationStatus VulkanRenderer::pipelinePreparationStatus(render::PipelinePreparationTicket ticket) const
{
    if (!pipelinePreparation_) throw std::out_of_range("pipeline preparation is not initialized");
    return pipelinePreparation_->status(ticket);
}
void VulkanRenderer::cancelPipelinePreparation(render::PipelinePreparationTicket ticket)
{
    if (!pipelinePreparation_) throw std::out_of_range("pipeline preparation is not initialized");
    pipelinePreparation_->cancel(ticket);
}
void VulkanRenderer::releasePipelinePreparation(render::PipelinePreparationTicket ticket)
{
    if (!pipelinePreparation_) throw std::out_of_range("pipeline preparation is not initialized");
    pipelinePreparation_->release(ticket);
}

void VulkanRenderer::beginScenePreparation(
    render::RenderAssetStore& renderAssets, render::SceneResourceRequest request)
{
    if (!*this || hasSceneResources() ||
        (scenePreparation_ && scenePreparation_->ownsResources()) || !renderAssets.empty() ||
        (resourcePreparation_ && !resourcePreparation_->empty()))
    {
        throw std::logic_error("scene preparation requires presentation and an empty scene/cache");
    }
    scenePreparation_ = std::make_unique<render::ScenePreparationSession>(
        *this, renderAssets, *resourcePreparation_, std::move(request));
    scenePreparation_->begin();
}

bool VulkanRenderer::canPrepareResources() const noexcept
{
    return resourcePreparation_ && !failed_ && !recordingFrame_ &&
        (!scenePreparation_ || !scenePreparation_->ownsResources());
}

render::ResourcePreparationService& VulkanRenderer::resourcePreparationService()
{
    if (!resourcePreparation_) throw std::out_of_range("resource preparation is not initialized");
    return *resourcePreparation_;
}

void VulkanRenderer::advanceResourcePreparation()
{
    if (recordingFrame_) throw std::logic_error("cannot publish resources during frame recording");
    if (!uploads_)
    {
        return;
    }
    uploads_->tick();
    resourcePreparation_->advance();
    if (scenePreparation_)
    {
        scenePreparation_->advance();
    }
    if (pipelinePreparation_) pipelinePreparation_->advance();
}

ResourcePreparationResult VulkanRenderer::prepareTexture(
    render::RenderAssetStore& cache, asset::AssetSnapshot<asset::TextureAsset> source)
{
    if (!resourcePreparation_ || (scenePreparation_ && scenePreparation_->ownsResources()))
    {
        return {ResourcePreparationCode::InvalidRequest,
                {},
                "texture preparation requires presentation and no scene preparation"};
    }
    return resourcePreparation_->prepareTexture(cache, std::move(source));
}

ResourcePreparationResult VulkanRenderer::prepareMesh(render::RenderAssetStore& cache,
                                                      asset::AssetSnapshot<asset::MeshAsset> source)
{
    if (!resourcePreparation_ || (scenePreparation_ && scenePreparation_->ownsResources()))
    {
        return {ResourcePreparationCode::InvalidRequest,
                {},
                "mesh preparation requires presentation and no scene preparation"};
    }
    return resourcePreparation_->prepareMesh(cache, std::move(source));
}

ResourcePreparationResult VulkanRenderer::prepareShader(
    render::RenderAssetStore& cache, asset::AssetSnapshot<asset::ShaderAsset> source)
{
    if (!resourcePreparation_ || (scenePreparation_ && scenePreparation_->ownsResources()))
    {
        return {ResourcePreparationCode::InvalidRequest,
                {},
                "resource preparation requires presentation and no scene preparation"};
    }
    return resourcePreparation_->prepareShader(cache, std::move(source));
}
ResourcePreparationResult VulkanRenderer::prepareShaderProgram(
    render::RenderAssetStore& cache, asset::AssetSnapshot<asset::ShaderProgramAsset> source)
{
    if (!resourcePreparation_ || (scenePreparation_ && scenePreparation_->ownsResources()))
    {
        return {ResourcePreparationCode::InvalidRequest,
                {},
                "resource preparation requires presentation and no scene preparation"};
    }
    return resourcePreparation_->prepareShaderProgram(cache, std::move(source));
}
ResourcePreparationResult VulkanRenderer::prepareMaterialTemplate(
    render::RenderAssetStore& cache, asset::AssetSnapshot<asset::MaterialTemplateAsset> source)
{
    if (!resourcePreparation_ || (scenePreparation_ && scenePreparation_->ownsResources()))
    {
        return {ResourcePreparationCode::InvalidRequest,
                {},
                "resource preparation requires presentation and no scene preparation"};
    }
    return resourcePreparation_->prepareMaterialTemplate(cache, std::move(source));
}
ResourcePreparationResult VulkanRenderer::prepareMaterial(
    render::RenderAssetStore& cache, asset::AssetSnapshot<asset::MaterialAsset> source)
{
    if (!resourcePreparation_ || (scenePreparation_ && scenePreparation_->ownsResources()))
    {
        return {ResourcePreparationCode::InvalidRequest,
                {},
                "resource preparation requires presentation and no scene preparation"};
    }
    return resourcePreparation_->prepareMaterial(cache, std::move(source));
}
void VulkanRenderer::setResourcePublicationTransaction(ResourcePreparationTicket ticket,
    std::shared_ptr<render::ResourcePublicationTransaction> publication)
{
    if (!resourcePreparation_) throw std::logic_error("resource preparation is unavailable");
    resourcePreparation_->setPublicationTransaction(ticket, std::move(publication));
}

ResourcePreparationStatus VulkanRenderer::resourcePreparationStatus(
    ResourcePreparationTicket ticket) const
{
    if (!resourcePreparation_)
    {
        throw std::out_of_range("unknown resource preparation ticket");
    }
    return resourcePreparation_->status(ticket);
}

void VulkanRenderer::cancelResourcePreparation(ResourcePreparationTicket ticket)
{
    if (!resourcePreparation_)
    {
        throw std::out_of_range("unknown resource preparation ticket");
    }
    resourcePreparation_->cancel(ticket);
}

void VulkanRenderer::releaseResourcePreparation(ResourcePreparationTicket ticket)
{
    if (!resourcePreparation_)
    {
        throw std::out_of_range("unknown resource preparation ticket");
    }
    resourcePreparation_->release(ticket);
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
        frameDataResources_.create(*resourceDevice_, count, maxRenderObjects);
        createSceneRenderTargets(extent(), count);
        if (outputMode_ == OutputMode::Editor)
        {
            createEditorViewportResources(extent(), count);
        }
        createPresentResources(count);
        scenePipelineDescription_ = describeGraphicsPipeline(makePipelineCreateInfo(), *resourceDevice_);
        graphicsPipeline_ = pipelineCache_->getOrCreate(scenePipelineDescription_);
        presentPipeline_ = preparePipeline(makePresentPipelineCreateInfo());
    }
    catch (...)
    {
        releaseSceneResources();
        throw;
    }
}

void VulkanRenderer::createPreparedSceneResources(
    std::shared_ptr<const render::PreparedMaterialTemplate> material,
    std::shared_ptr<const render::PreparedShaderProgram> present, uint32_t maxRenderObjects)
{
    createSceneResources(makeDefaultScenePipeline(material->program(), material->layoutReference()),
        makeDefaultPresentPipeline(std::move(present)), maxRenderObjects);
}

void VulkanRenderer::releasePreparedSceneResources() noexcept
{
    releaseSceneResources();
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
        collectRetiredResourcesAfterIdle();
    }
    presentPipeline_.reset();
    graphicsPipeline_.reset();
    scenePipelineDescription_ = {};
    if (pipelinePreparation_) pipelinePreparation_->cancelAll();
    if (pipelineCache_) pipelineCache_->clear();
    outputBindings_.reset();
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
    resourcePreparation_.reset();
    resourceUploads_.reset();
    if (uploads_) uploads_->shutdown();
    uploads_.reset();
    pipelinePreparation_.reset();
    pipelineCache_.reset();
    pipelineDevice_.reset();
    frameSlots_.clear();
    scheduler_.reset();
    failed_ = false;
    recordingFrame_ = false;
    resourceDevice_.reset();
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

void VulkanRenderer::retireExternalResource(std::shared_ptr<const void> resource)
{
    if (!*this || !resource)
        throw std::invalid_argument("retirement requires a renderer and an owned resource");
    scheduler_->pending().retire(std::move(resource));
}

void VulkanRenderer::drainRetiredResources()
{
    waitIdle();
    collectRetiredResourcesAfterIdle();
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
    collectRetiredResourcesAfterIdle();

    VkExtent2D renderExtent = framebufferExtent;
    if (outputMode_ == OutputMode::Editor &&
        !editorViewportTargets_.empty())
    {
        renderExtent = editorViewportTargets_.front().extent();
    }

    SwapchainResources::CreateInfo createInfo{};
    createInfo.surface = context_->surface();
    createInfo.framebufferExtent = framebufferExtent;
    static_cast<void>(swapchainResources_.recreate(device, createInfo));
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
            static_cast<uint32_t>(frameSlots_.size()),
            sceneColorFormat_,
            sceneDepthFormat_);
    std::vector<RenderTarget> newEditorViewportTargets;
    if (outputMode_ == OutputMode::Editor)
    {
        newEditorViewportTargets = makeEditorViewportTargets(
            device,
            editorViewportRenderPass_.get(),
            renderExtent,
            static_cast<uint32_t>(frameSlots_.size()),
            editorViewportFormat_);
    }
    sceneRenderTargets_ = std::move(newSceneRenderTargets);
    if (outputMode_ == OutputMode::Editor)
    {
        editorViewportTargets_ = std::move(newEditorViewportTargets);
        ++editorViewportRevision_;
    }
    recreatePresentDescriptorSets(
        static_cast<uint32_t>(frameSlots_.size()));
    presentOutputTransferFunction_ =
        render::outputTransferFunction(fromVulkanFormat(swapchainResources_.format()),
            swapchainResources_.colorSpace() == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);

    if (outputMode_ == OutputMode::Runtime)
    {
        // The complete key decides compatibility, including recreated pass/layout objects.
        presentPipeline_ = preparePipeline(makePresentPipelineCreateInfo());
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
            static_cast<uint32_t>(frameSlots_.size()),
            sceneColorFormat_,
            sceneDepthFormat_);
    std::vector<RenderTarget> newEditorViewportTargets =
        makeEditorViewportTargets(
            device,
            editorViewportRenderPass_.get(),
            extent,
            static_cast<uint32_t>(frameSlots_.size()),
            editorViewportFormat_);
    std::vector<rhi::TextureViewRef> resolveViews;
    for (const auto& target : newSceneRenderTargets) resolveViews.push_back(target.view(kSceneResolveAttachment));
    auto newOutputBindings = outputBindings_;
    newOutputBindings.rebind(*resourceDevice_, resolveViews);

    // Recorded commands and presentation descriptors reference the previous
    // scene targets.
    collectRetiredResourcesAfterIdle();
    sceneRenderTargets_ = std::move(newSceneRenderTargets);
    editorViewportTargets_ = std::move(newEditorViewportTargets);
    outputBindings_ = std::move(newOutputBindings);
    ++editorViewportRevision_;
}


VulkanRenderer::RenderResult VulkanRenderer::render(
    const render::RenderFrame& frameData,
    const render::RenderAssetStore& renderAssets,
    const render::GuiOverlay* uiDrawData)
{
    if (!sceneReady())
    {
        throw std::logic_error("scene resources are not ready");
    }
    return renderFrame(&frameData, &renderAssets, uiDrawData);
}

VulkanRenderer::RenderResult VulkanRenderer::renderGui(const render::GuiOverlay* uiDrawData)
{
    return renderFrame(nullptr, nullptr, uiDrawData);
}

VulkanRenderer::RenderResult VulkanRenderer::renderFrame(
    const render::RenderFrame* sceneFrame,
    const render::RenderAssetStore* renderAssets,
    const render::GuiOverlay* uiDrawData)
{
    if (!*this)
    {
        throw std::logic_error("cannot render with an uninitialized VulkanRenderer");
    }
    if (failed_) throw std::logic_error("presentation session failed; recreate it before rendering");
    if (recordingFrame_) throw std::logic_error("recursive frame recording");
    recordingFrame_ = true;
    struct RecordingScope { bool& active; ~RecordingScope() { active = false; } } recordingScope{recordingFrame_};
    render::PreparedDrawList drawList;
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
        drawList = compileDrawList(frameData.renderList, *renderAssets);
    }

    const Device& device = context_->device();
    RenderFrameSlot& slot = frameSlots_[currentFrame_];
    FrameContext& frame = slot.context;
    frame.waitUntilReusable();
    frame.resetCommands();
    scheduler_->beginReusableSlot(currentFrame_);

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
    // Any exception after acquisition terminates this presentation session.
    // Its acquire semaphore/image and possibly reset fence cannot be retried.
    bool frameFinished = false;
    struct AcquiredScope {
        bool& failed; bool& finished;
        ~AcquiredScope() { if (!finished) failed = true; }
    } acquiredScope{failed_, frameFinished};
    bool needsResize = acquireResult == VK_SUBOPTIMAL_KHR;

    swapchainResources_.waitUntilImageReusable(device, imageIndex);
    swapchainResources_.markImageInFlight(
        imageIndex,
        frame.inFlightFence());

    if (sceneFrame != nullptr)
    {
        updateFrameData(currentFrame_, *sceneFrame);
    }
    recordCommandBuffer(
        frame.commandBuffer(),
        currentFrame_,
        imageIndex,
        sceneFrame != nullptr ? frameDataResources_.bindingSet(currentFrame_) : rhi::BindingSetRef{},
        drawList,
        uiDrawData,
        sceneFrame != nullptr);

    const rhi::CompletionPoint submittedPoint = scheduler_->candidate();
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

    // No allocation or destruction after submit: associate only with this NEW
    // submission, never the previous fence cycle we waited for above. Early
    // acquire returns and exceptions before submit leave pending retirement intact.
    scheduler_->commitSuccess(currentFrame_, submittedPoint);

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

    frameFinished = true;
    currentFrame_ =
        (currentFrame_ + 1) % static_cast<uint32_t>(frameSlots_.size());
    return needsResize
        ? RenderResult::RenderedNeedsResize
        : RenderResult::Rendered;
}

VulkanRenderer::operator bool() const noexcept
{
    return context_ != nullptr && static_cast<bool>(swapchainResources_) &&
        !frameSlots_.empty();
}

bool VulkanRenderer::sceneReady() const noexcept
{
    return (!scenePreparation_ || !scenePreparation_->ownsResources()) && hasSceneResources();
}

bool VulkanRenderer::hasSceneResources() const noexcept
{
    const bool editorResourcesValid = outputMode_ != OutputMode::Editor ||
        (static_cast<bool>(editorViewportRenderPass_) &&
         editorViewportTargets_.size() == frameSlots_.size() &&
         editorViewportFormat_ != VK_FORMAT_UNDEFINED &&
         editorViewportRevision_ != 0);
    return context_ != nullptr &&
        frameDataResources_.frameCount() != 0 &&
        static_cast<bool>(swapchainResources_) &&
        static_cast<bool>(sceneRenderPass_) &&
        sceneRenderTargets_.size() == frameSlots_.size() &&
        static_cast<bool>(outputBindings_.layout()) &&
        outputBindings_.size() == frameSlots_.size() &&
        static_cast<bool>(graphicsPipeline_) &&
        static_cast<bool>(presentPipeline_) &&
        !frameSlots_.empty() &&
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
        target.view(0),
        {target.extent().width, target.extent().height},
        editorViewportRevision_
    };
}

void VulkanRenderer::collectRetiredResourcesAfterIdle()
{
    for (RenderFrameSlot& slot : frameSlots_)
    {
        slot.context.resetCommands();
    }
    if (scheduler_) scheduler_->collectAfterIdle();
}

void VulkanRenderer::createFrameSlots(uint32_t frameCount)
{
    const Device& device = context_->device();
    std::vector<RenderFrameSlot> newContexts;
    newContexts.reserve(frameCount);
    for (uint32_t i = 0; i < frameCount; ++i)
    {
        newContexts.emplace_back(device);
    }
    frameSlots_ = std::move(newContexts);
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
    validateSceneSampleCount(device, colorFormat, depthFormat);
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
            VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT);
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
    if (frameCount != sceneRenderTargets_.size()) throw std::logic_error("output frame count mismatch");
    std::vector<rhi::TextureViewRef> views;
    for (const auto& target : sceneRenderTargets_) views.push_back(target.view(kSceneResolveAttachment));
    outputBindings_.create(*resourceDevice_, views);
    presentOutputTransferFunction_ = render::outputTransferFunction(fromVulkanFormat(swapchainResources_.format()),
        swapchainResources_.colorSpace() == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);
}

void VulkanRenderer::recreatePresentDescriptorSets(uint32_t frameCount)
{
    if (frameCount != sceneRenderTargets_.size()) throw std::logic_error("output frame count mismatch");
    std::vector<rhi::TextureViewRef> views;
    for (const auto& target : sceneRenderTargets_) views.push_back(target.view(kSceneResolveAttachment));
    outputBindings_.rebind(*resourceDevice_, views);
}

render::PreparedDrawList VulkanRenderer::compileDrawList(const render::RenderList& source,
                                              const render::RenderAssetStore& resources)
{
    if (!pipelineCache_ || !sceneRenderPass_ || !frameDataResources_.bindingLayout())
        throw std::logic_error("draw compilation requires initialized scene resources");
    return render::DrawCompiler{}.compile(source, resources, scenePipelineDescription_, *pipelineCache_);
}

GraphicsPipeline::CreateInfo VulkanRenderer::makePipelineCreateInfo() const
{
    GraphicsPipeline::CreateInfo createInfo = pipelineCreateInfo_;
    createInfo.renderPass = sceneRenderPass_.reference();
    // Pass-owned state is shared by bootstrap, prewarm and per-material draws.
    createInfo.multisample.samples = kSceneSamples;
    createInfo.descriptorSetLayouts.insert(
        createInfo.descriptorSetLayouts.begin(),
        nativeBindingLayout(frameDataResources_.bindingLayout()));
    return createInfo;
}

GraphicsPipeline::CreateInfo
VulkanRenderer::makePresentPipelineCreateInfo() const
{
    GraphicsPipeline::CreateInfo createInfo = presentPipelineCreateInfo_;
    createInfo.renderPass = outputMode_ == OutputMode::Editor
        ? editorViewportRenderPass_.reference()
        : swapchainResources_.renderPassReference();
    createInfo.descriptorSetLayouts.insert(
        createInfo.descriptorSetLayouts.begin(),
        nativeBindingLayout(outputBindings_.layout()));
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

void VulkanRenderer::recordCommandBuffer(VkCommandBuffer commandBuffer,
    uint32_t frameIndex, uint32_t imageIndex, const rhi::BindingSetRef& descriptorSet,
    const render::PreparedDrawList& drawList, const render::GuiOverlay* uiDrawData, bool drawScene)
{
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
        throw std::runtime_error("failed to begin recording command buffer");

    VulkanCommandEncoder commands(resourceDevice_->id(), commandBuffer);
    VulkanGuiCommandContext guiContext{commandBuffer};
    VulkanPassTarget presentation(resourceDevice_->id(), swapchainResources_.renderPassReference(),
        swapchainResources_.framebuffer(imageIndex), swapchainResources_.extent());
    std::optional<VulkanPassTarget> scene, viewport;
    render::FramePasses passes;
    passes.presentation = &presentation;
    if (drawScene)
    {
        const auto& target = sceneRenderTargets_.at(frameIndex);
        scene.emplace(resourceDevice_->id(), sceneRenderPass_.reference(), target.framebuffer(), target.extent());
        passes.scene = &*scene;
        passes.sceneResolve = target.texture(kSceneResolveAttachment);
        passes.frameBindings = descriptorSet;
        passes.outputBindings = outputBindings_.set(frameIndex);
        passes.outputPipeline = presentPipeline_;
        passes.outputTransfer = presentOutputTransferFunction_;
        if (outputMode_ == OutputMode::Editor)
        {
            const auto& output = editorViewportTargets_.at(frameIndex);
            viewport.emplace(resourceDevice_->id(), editorViewportRenderPass_.reference(), output.framebuffer(), output.extent());
            passes.viewport = &*viewport;
        }
    }
    render::recordFramePipeline(commands, outputMode_, passes, drawScene ? &drawList : nullptr, uiDrawData, guiContext);
    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
        throw std::runtime_error("failed to record command buffer");
}











} // namespace rubia::rhi::vulkan
