#pragma once

#include "vulkan/DescriptorPool.hpp"
#include "vulkan/DescriptorSetLayout.hpp"
#include "vulkan/FrameContext.hpp"
#include "vulkan/FrameDataResources.hpp"
#include "vulkan/GraphicsPipeline.hpp"
#include "render/RenderFrame.hpp"
#include "render/SceneResourcePreparation.hpp"
#include "vulkan/VulkanUploadService.hpp"
#include "vulkan/RenderPass.hpp"
#include "vulkan/RenderTarget.hpp"
#include "vulkan/Sampler.hpp"
#include "vulkan/SwapchainResources.hpp"

#include <cstdint>
#include <memory>
#include <vector>

struct ImDrawData;

namespace rubia::rhi::vulkan
{

class Mesh;
class RenderAssetCache;
class VulkanContext;
class VulkanScenePreparation;
class GpuTexture;
struct VulkanDrawList;

// Owns the Vulkan objects and synchronization needed to execute one render
// target. Scene objects and asset ownership stay outside this class.
class VulkanRenderer final
{
public:
    enum class OutputMode
    {
        Runtime,
        Editor
    };

    struct EditorViewportOutput
    {
        VkImageView imageView = VK_NULL_HANDLE;
        VkExtent2D extent{};
        uint64_t revision = 0;
    };

    /// Parameters used to create the renderer and its frame resources.
    struct CreateInfo
    {
        /// Non-owning context used for all Vulkan operations.
        VulkanContext* context = nullptr;
        /// Initial drawable size of the render target.
        VkExtent2D framebufferExtent{};
        /// Number of CPU/GPU frame slots used concurrently.
        uint32_t framesInFlight = 2;
        /// Maximum number of render objects accepted per frame.
        uint32_t maxRenderObjects = 1024;
        /// Selects direct runtime presentation or an ImGui-sampled LDR output.
        OutputMode outputMode = OutputMode::Runtime;

        // renderPass and descriptorSetLayouts are supplied by VulkanRenderer.
        /// Caller-supplied graphics pipeline settings.
        GraphicsPipeline::CreateInfo graphicsPipeline;
        /// Caller-supplied final presentation pipeline settings.
        GraphicsPipeline::CreateInfo presentPipeline;
    };

    /// Outcome of a render attempt.
    enum class RenderResult
    {
        /// The frame was submitted successfully.
        Rendered,
        /// The swapchain must be resized before rendering can continue.
        NeedsResize
    };

    /// Creates an empty renderer.
    VulkanRenderer();
    /// Creates a renderer from the supplied settings.
    VulkanRenderer(const CreateInfo& createInfo);
    /// Releases all renderer-owned resources.
    ~VulkanRenderer();

    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;
    /// Renderer ownership cannot be moved because it retains context-bound state.
    VulkanRenderer(VulkanRenderer&&) = delete;
    /// Renderer ownership cannot be move-assigned.
    VulkanRenderer& operator=(VulkanRenderer&&) = delete;

    /// Creates or replaces all renderer-owned resources.
    void create(const CreateInfo& createInfo);
    /// Creates a GUI-capable swapchain without any scene assets or shaders.
    void createPresentation(const CreateInfo& createInfo);
    /// Main-thread-only preparation into an empty scene/cache. The cache must
    /// outlive this renderer and remain untouched during preparation. CPU assets
    /// are retained until activation, cancellation, or failure.
    void beginScenePreparation(
        RenderAssetCache& renderAssets, render::SceneResourceRequest request);
    /// Polls uploads and advances one soft-budgeted batch or the pipeline stage.
    void advanceScenePreparation();
    // Called once per frame, including when no scene is loading.
    void advanceResourcePreparation();
    // Initial, incremental texture preparation. Source remains immutable; cache
    // must outlive the ticket (and all published GPU uses). No replacement here.
    UploadEnqueueResult prepareTexture(RenderAssetCache& cache,
        asset::TextureAssetHandle handle, std::shared_ptr<const asset::TextureAsset> source);
    // Explicit blocking path for transactional replacement. Uses the shared service;
    // drains accepted uploads and returns an unpublished texture. Caller synchronizes
    // existing descriptor users before committing replacement into the live cache.
    [[nodiscard]] GpuTexture uploadTextureAndWait(
        std::shared_ptr<const asset::TextureAsset> source);
    UploadStatus texturePreparationStatus(UploadTicket ticket) const;
    void cancelTexturePreparation(UploadTicket ticket);
    void releaseTexturePreparation(UploadTicket ticket);

    [[nodiscard]] render::ScenePreparationStatus scenePreparationStatus() const;
    /// Publishes ready GPU resources; caller then publishes matching CPU content.
    void activatePreparedScene();
    /// Cancels pending/ready work and safely releases its partial resources.
    /// May wait for in-flight uploads. Activated scene resources are unaffected.
    void cancelScenePreparation() noexcept;
    /// Adds scene rendering to an existing presentation session.
    void createSceneResources(
        const GraphicsPipeline::CreateInfo& graphicsPipeline,
        const GraphicsPipeline::CreateInfo& presentPipeline,
        uint32_t maxRenderObjects = 1024);
    /// Releases scene resources while keeping the GUI presentation alive.
    void resetSceneResources() noexcept;
    [[nodiscard]] bool sceneReady() const noexcept;
    /// Releases all renderer-owned resources and cached frame data.
    void reset() noexcept;
    /// Waits until all device work has completed.
    void waitIdle() const;

    /// Rebuilds resources that depend on the framebuffer size.
    void resize(VkExtent2D framebufferExtent);
    /// Rebuilds Editor scene outputs at the Scene View's pixel dimensions.
    void resizeEditorViewport(VkExtent2D extent);
    /// Records, submits, and presents one scene snapshot.
    [[nodiscard]] RenderResult render(
        const render::RenderFrame& frame,
        const RenderAssetCache& renderAssets,
        ImDrawData* uiDrawData = nullptr);
    [[nodiscard]] RenderResult renderGui(ImDrawData* uiDrawData);

    /// Returns the current swapchain extent.
    [[nodiscard]] VkExtent2D extent() const noexcept
    {
        return swapchainResources_.extent();
    }
    /// Returns the render pass used to write the presentation image.
    [[nodiscard]] VkRenderPass presentRenderPass() const noexcept
    {
        return swapchainResources_.renderPass();
    }
    /// Returns the number of images in the current presentation swapchain.
    [[nodiscard]] uint32_t swapchainImageCount() const noexcept
    {
        return static_cast<uint32_t>(swapchainResources_.imageCount());
    }
    /// Returns the frame slot that will be used by the next render call.
    [[nodiscard]] uint32_t currentFrameIndex() const noexcept
    {
        return currentFrame_;
    }
    [[nodiscard]] uint32_t frameCount() const noexcept
    {
        return static_cast<uint32_t>(frameContexts_.size());
    }
    [[nodiscard]] OutputMode outputMode() const noexcept
    {
        return outputMode_;
    }
    /// Returns one Editor LDR image suitable for ImGui texture registration.
    [[nodiscard]] EditorViewportOutput editorViewportOutput(
        uint32_t frameIndex) const;
    /// Returns whether base presentation is initialized; sceneReady is separate.
    [[nodiscard]] explicit operator bool() const noexcept;

private:
    friend class VulkanScenePreparation;
    [[nodiscard]] bool hasSceneResources() const noexcept;
    void releaseSceneResources() noexcept;
    [[nodiscard]] RenderResult renderFrame(
        const render::RenderFrame* frame,
        const RenderAssetCache* renderAssets,
        ImDrawData* uiDrawData);
    /// Creates one reusable command and synchronization context per frame slot.
    void createFrameContexts(uint32_t frameCount);
    /// Completes pipeline settings with renderer-owned layouts and render pass.
    [[nodiscard]] GraphicsPipeline::CreateInfo makePipelineCreateInfo() const;
    /// Completes final presentation pipeline settings.
    [[nodiscard]] GraphicsPipeline::CreateInfo
    makePresentPipelineCreateInfo() const;
    /// Creates one offscreen color/depth framebuffer per frame slot.
    void createSceneRenderTargets(VkExtent2D extent, uint32_t frameCount);
    /// Creates one tone-mapped LDR output per frame slot in Editor mode.
    void createEditorViewportResources(
        VkExtent2D extent,
        uint32_t frameCount);
    /// Creates the sampler, descriptor layout, pool, and sets for presentation.
    void createPresentResources(uint32_t frameCount);
    /// Replaces descriptor sets after scene color views are recreated.
    void recreatePresentDescriptorSets(uint32_t frameCount);
    /// Stages and uploads camera and object data for one frame slot.
    void updateFrameData(uint32_t frameIndex, const render::RenderFrame& frame);
    /// Records all draw commands for one acquired swapchain image.
    void recordCommandBuffer(
        VkCommandBuffer commandBuffer,
        uint32_t frameIndex,
        uint32_t imageIndex,
        VkDescriptorSet descriptorSet,
        const VulkanDrawList& drawList,
        ImDrawData* uiDrawData,
        bool drawScene);
    /// Records all scene draws into the offscreen target for one frame slot.
    void recordScenePass(
        VkCommandBuffer commandBuffer,
        uint32_t frameIndex,
        VkDescriptorSet descriptorSet,
        const VulkanDrawList& drawList);
    /// Makes offscreen color writes visible to the presentation shader.
    void transitionSceneColorForSampling(
        VkCommandBuffer commandBuffer,
        uint32_t frameIndex);
    /// Records the full-screen draw into one acquired swapchain image.
    void recordPresentPass(
        VkCommandBuffer commandBuffer,
        uint32_t frameIndex,
        uint32_t imageIndex,
        ImDrawData* uiDrawData);
    /// Tone maps the current HDR scene into the Editor LDR target.
    void recordEditorViewportPass(
        VkCommandBuffer commandBuffer,
        uint32_t frameIndex);
    /// Clears the Editor swapchain and renders only ImGui draw data.
    void recordEditorUiPass(
        VkCommandBuffer commandBuffer,
        uint32_t imageIndex,
        ImDrawData* uiDrawData);

    // Declaration order encodes destruction dependencies:
    // frames -> pipelines -> present descriptors -> scene targets
    // -> scene render pass -> swapchain -> frame-data resources.
    /// Non-owning context that must outlive the renderer.
    VulkanContext* context_ = nullptr;
    /// Pipeline settings retained for swapchain-driven rebuilds.
    GraphicsPipeline::CreateInfo pipelineCreateInfo_;
    /// Presentation pipeline settings retained for format-driven rebuilds.
    GraphicsPipeline::CreateInfo presentPipelineCreateInfo_;
    OutputMode outputMode_ = OutputMode::Runtime;
    /// Per-frame descriptors and camera/object buffers.
    FrameDataResources frameDataResources_;
    /// Swapchain and all resources tied to its images.
    SwapchainResources swapchainResources_;
    /// Render pass describing the offscreen linear-color and depth outputs.
    RenderPass sceneRenderPass_;
    /// One independently reusable offscreen framebuffer per frame slot.
    std::vector<RenderTarget> sceneRenderTargets_;
    /// Linear HDR color format shared by the scene targets.
    VkFormat sceneColorFormat_ = VK_FORMAT_UNDEFINED;
    /// Depth-stencil format shared by the scene targets.
    VkFormat sceneDepthFormat_ = VK_FORMAT_UNDEFINED;
    /// Render pass that tone maps HDR scene color into an Editor LDR image.
    RenderPass editorViewportRenderPass_;
    /// One ImGui-sampled LDR image per frame slot.
    std::vector<RenderTarget> editorViewportTargets_;
    VkFormat editorViewportFormat_ = VK_FORMAT_UNDEFINED;
    uint64_t editorViewportRevision_ = 0;
    /// Linear clamp sampler used by the presentation shader.
    Sampler presentSampler_;
    /// Descriptor interface used to sample one offscreen color image.
    DescriptorSetLayout presentDescriptorSetLayout_;
    /// Owns the per-frame presentation descriptor sets.
    DescriptorPool presentDescriptorPool_;
    /// Non-owning sets allocated from presentDescriptorPool_, indexed by F.
    std::vector<VkDescriptorSet> presentDescriptorSets_;
    /// Graphics pipeline used to record scene draws.
    GraphicsPipeline graphicsPipeline_;
    /// Full-screen pipeline used to write the acquired swapchain image.
    GraphicsPipeline presentPipeline_;
    /// Shader output transfer selected from swapchain format and color space.
    uint32_t presentOutputTransferFunction_ = 0;
    /// Reusable command and synchronization resources for in-flight frames.
    std::vector<FrameContext> frameContexts_;
    /// Frame slot selected for the next submission.
    uint32_t currentFrame_ = 0;
    /// Identity of the view whose GPU payload is currently staged.
    render::RenderViewId stagedViewId_{};
    /// Revision of the currently staged view GPU payload.
    uint64_t stagedViewGpuDataRevision_ = 0;
    // Destroy/cancel before scene objects, frame contexts, cache, or device.
    struct PendingTexturePreparation
    {
        RenderAssetCache* cache = nullptr;
        asset::TextureAssetHandle handle;
        std::shared_ptr<GpuTexture> texture;
        bool published = false;
        bool cancelled = false;
        std::string error;
    };
    std::unique_ptr<VulkanUploadService> uploads_; //长期存在的上传服务。场景加载或者独立asset准备都使用此服务
    std::map<uint64_t, PendingTexturePreparation> pendingTextures_;
    std::unique_ptr<VulkanScenePreparation> scenePreparation_; 
};

} // namespace rubia::rhi::vulkan
