#pragma once

#include "render/ApplicationGuiRenderBridge.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>
#include <memory>

namespace rubia::rhi::vulkan
{

class RenderAssetCache;
class VulkanRenderer;

/// Adapts Vulkan render outputs and cached textures to opaque GUI tokens.
class VulkanApplicationGuiRenderBridge final
    : public render::ApplicationGuiRenderBridge
{
public:
    ~VulkanApplicationGuiRenderBridge() override;

    void attach(
        VulkanRenderer& renderer,
        const RenderAssetCache& renderAssets);
    void detach() noexcept;

    void resizeSceneViewport(uint32_t width, uint32_t height) override;
    [[nodiscard]] render::ApplicationGuiRenderFrame currentFrame() override;
    /// Resolve each GUI frame; do not reuse tokens across texture publications.
    [[nodiscard]] render::ApplicationGuiTexture preview(
        asset::TextureAssetHandle texture) override;
    /// Explicit synchronous invalidation; caller must have completed GPU users.
    /// Ordinary publication changes are handled by preview() with retirement.
    void invalidatePreview(asset::TextureAssetHandle texture) noexcept override;

private:
    struct PreviewDescriptor;
    struct TextureEntry
    {
        asset::TextureAssetHandle texture;
        std::shared_ptr<const PreviewDescriptor> descriptor;
        uint64_t publication = 0;
    };

    void registerViewportTextures();
    void refreshViewportTexturesIfNeeded();
    void releaseViewportTextures() noexcept;
    void releasePreviewTextures() noexcept;

    VulkanRenderer* renderer_ = nullptr;
    const RenderAssetCache* renderAssets_ = nullptr;
    std::vector<VkDescriptorSet> viewportTextures_;
    std::vector<TextureEntry> previewTextures_;
    uint64_t viewportTextureRevision_ = 0;
};

} // namespace rubia::rhi::vulkan
