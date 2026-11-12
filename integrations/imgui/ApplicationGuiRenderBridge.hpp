#pragma once

#include "render/ApplicationGuiRenderBridge.hpp"
#include "resources/RenderAssetStore.hpp"
#include "core/GuiRenderHost.hpp"
#include "imgui/GuiBackend.hpp"

#include <cstdint>
#include <vector>
#include <memory>

namespace rubia::integration::imgui
{

/// Adapts scene outputs and resident textures through the selected GUI backend.
class ApplicationGuiRenderBridge final
    : public render::ApplicationGuiRenderBridge
{
public:
    ~ApplicationGuiRenderBridge() override;

    // Host, asset store and backend outlive this bridge. Detach before backend shutdown.
    void attach(
        render::GuiRenderHost& renderer,
        const render::RenderAssetStore& renderAssets,
        GuiRendererBackend& backend);
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
    struct TextureEntry
    {
        asset::TextureAssetHandle texture;
        GuiTextureRef descriptor;
        uint64_t publication = 0;
    };

    void registerViewportTextures();
    void refreshViewportTexturesIfNeeded();
    void releaseViewportTextures() noexcept;
    void releasePreviewTextures() noexcept;

    render::GuiRenderHost* renderer_ = nullptr;
    const render::RenderAssetStore* renderAssets_ = nullptr;
    GuiRendererBackend* backend_ = nullptr;
    std::vector<GuiTextureRef> viewportTextures_;
    std::vector<TextureEntry> previewTextures_;
    uint64_t viewportTextureRevision_ = 0;
};

} // namespace rubia::integration::imgui
