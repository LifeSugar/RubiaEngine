#include "vulkan/VulkanApplicationGuiRenderBridge.hpp"

#include "vulkan/GpuTexture.hpp"
#include "vulkan/RenderAssetCache.hpp"
#include "vulkan/VulkanRenderer.hpp"

#include <imgui_impl_vulkan.h>

#include <algorithm>
#include <stdexcept>

namespace rubia::rhi::vulkan
{

VulkanApplicationGuiRenderBridge::~VulkanApplicationGuiRenderBridge()
{
    detach();
}

void VulkanApplicationGuiRenderBridge::attach(
    VulkanRenderer& renderer,
    const RenderAssetCache& renderAssets)
{
    detach();
    renderer_ = &renderer;
    renderAssets_ = &renderAssets;

    if (renderer.sceneReady() &&
        renderer.outputMode() == VulkanRenderer::OutputMode::Editor)
    {
        registerViewportTextures();
    }
}

void VulkanApplicationGuiRenderBridge::detach() noexcept
{
    releasePreviewTextures();
    releaseViewportTextures();
    renderAssets_ = nullptr;
    renderer_ = nullptr;
}

void VulkanApplicationGuiRenderBridge::resizeSceneViewport(
    uint32_t width,
    uint32_t height)
{
    if (renderer_ == nullptr || !renderer_->sceneReady() ||
        renderer_->outputMode() != VulkanRenderer::OutputMode::Editor ||
        width == 0 || height == 0)
    {
        return;
    }

    const VkExtent2D requestedExtent{width, height};
    const VulkanRenderer::EditorViewportOutput current =
        renderer_->editorViewportOutput(0);
    if (current.extent.width == requestedExtent.width &&
        current.extent.height == requestedExtent.height)
    {
        return;
    }

    // ImGui descriptors must stop referring to the old image views before
    // those views are destroyed. Waiting also makes all old frame commands
    // and descriptor uses reusable.
    renderer_->waitIdle();
    releaseViewportTextures();
    try
    {
        renderer_->resizeEditorViewport(requestedExtent);
        registerViewportTextures();
    }
    catch (...)
    {
        // Renderer resizing has strong resource replacement semantics, so the
        // previous outputs remain available if allocation fails.
        registerViewportTextures();
        throw;
    }
}

render::ApplicationGuiRenderFrame
VulkanApplicationGuiRenderBridge::currentFrame()
{
    if (renderer_ == nullptr)
    {
        return {};
    }
    if (!renderer_->sceneReady())
    {
        const VkExtent2D extent = renderer_->extent();
        render::ApplicationGuiRenderFrame frame{};
        frame.width = extent.width;
        frame.height = extent.height;
        return frame;
    }

    render::ApplicationGuiRenderFrame frame{};

    if (renderer_->outputMode() != VulkanRenderer::OutputMode::Editor)
    {
        const VkExtent2D extent = renderer_->extent();
        frame.width = extent.width;
        frame.height = extent.height;
        return frame;
    }

    refreshViewportTexturesIfNeeded();
    const uint32_t frameIndex = renderer_->currentFrameIndex();
    if (frameIndex < viewportTextures_.size())
    {
        const VulkanRenderer::EditorViewportOutput output =
            renderer_->editorViewportOutput(frameIndex);
        frame.width = output.extent.width;
        frame.height = output.extent.height;
        frame.sceneViewport.textureId = reinterpret_cast<std::uintptr_t>(
            viewportTextures_[frameIndex]);
    }
    return frame;
}

render::ApplicationGuiTexture VulkanApplicationGuiRenderBridge::preview(
    asset::TextureAssetHandle texture)
{
    if (renderAssets_ == nullptr || !texture)
    {
        return {};
    }

    const GpuTexture* gpuTexture = renderAssets_->tryTexture(texture);
    const auto publication = renderAssets_->texturePublication(texture);
    const auto existing =
        std::find_if(previewTextures_.begin(), previewTextures_.end(),
                     [texture](const TextureEntry& entry) { return entry.texture == texture; });
    if (existing != previewTextures_.end())
    {
        if (gpuTexture && existing->publication == publication)
        {
            return {reinterpret_cast<std::uintptr_t>(existing->descriptor)};
        }
        // The descriptor may still be referenced by an earlier submitted GUI frame.
        renderer_->waitIdle();
        invalidatePreview(texture);
    }
    if (!gpuTexture)
    {
        return {};
    }

    const VkDescriptorSet descriptor = ImGui_ImplVulkan_AddTexture(
        gpuTexture->view(),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (descriptor == VK_NULL_HANDLE)
    {
        throw std::runtime_error(
            "failed to register texture preview with ImGui");
    }

    previewTextures_.push_back({texture, descriptor, publication});
    return {reinterpret_cast<std::uintptr_t>(descriptor)};
}

void VulkanApplicationGuiRenderBridge::invalidatePreview(
    asset::TextureAssetHandle texture) noexcept
{
    const auto firstRemoved = std::remove_if(
        previewTextures_.begin(),
        previewTextures_.end(),
        [texture](const TextureEntry& entry)
        {
            if (entry.texture != texture)
            {
                return false;
            }
            if (entry.descriptor != VK_NULL_HANDLE)
            {
                ImGui_ImplVulkan_RemoveTexture(entry.descriptor);
            }
            return true;
        });
    previewTextures_.erase(firstRemoved, previewTextures_.end());
}

void VulkanApplicationGuiRenderBridge::registerViewportTextures()
{
    if (renderer_ == nullptr || !renderer_->sceneReady() ||
        renderer_->outputMode() != VulkanRenderer::OutputMode::Editor ||
        renderer_->frameCount() == 0)
    {
        return;
    }

    releaseViewportTextures();
    viewportTextures_.reserve(renderer_->frameCount());
    try
    {
        for (uint32_t frameIndex = 0;
             frameIndex < renderer_->frameCount();
             ++frameIndex)
        {
            const VulkanRenderer::EditorViewportOutput output =
                renderer_->editorViewportOutput(frameIndex);
            const VkDescriptorSet texture = ImGui_ImplVulkan_AddTexture(
                output.imageView,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            if (texture == VK_NULL_HANDLE)
            {
                throw std::runtime_error(
                    "failed to register Editor viewport texture with ImGui");
            }
            viewportTextures_.push_back(texture);
            viewportTextureRevision_ = output.revision;
        }
    }
    catch (...)
    {
        releaseViewportTextures();
        throw;
    }
}

void VulkanApplicationGuiRenderBridge::refreshViewportTexturesIfNeeded()
{
    if (renderer_ == nullptr || !renderer_->sceneReady() || renderer_->frameCount() == 0)
    {
        return;
    }

    const VulkanRenderer::EditorViewportOutput output =
        renderer_->editorViewportOutput(0);
    if (viewportTextures_.size() != renderer_->frameCount() ||
        viewportTextureRevision_ != output.revision)
    {
        registerViewportTextures();
    }
}

void VulkanApplicationGuiRenderBridge::releaseViewportTextures() noexcept
{
    for (VkDescriptorSet texture : viewportTextures_)
    {
        if (texture != VK_NULL_HANDLE)
        {
            ImGui_ImplVulkan_RemoveTexture(texture);
        }
    }
    viewportTextures_.clear();
    viewportTextureRevision_ = 0;
}

void VulkanApplicationGuiRenderBridge::releasePreviewTextures() noexcept
{
    for (const TextureEntry& entry : previewTextures_)
    {
        if (entry.descriptor != VK_NULL_HANDLE)
        {
            ImGui_ImplVulkan_RemoveTexture(entry.descriptor);
        }
    }
    previewTextures_.clear();
}

} // namespace rubia::rhi::vulkan
