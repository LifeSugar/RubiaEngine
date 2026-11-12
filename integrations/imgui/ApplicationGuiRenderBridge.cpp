#include "imgui/ApplicationGuiRenderBridge.hpp"

#include <algorithm>
#include <stdexcept>

namespace rubia::integration::imgui
{

ApplicationGuiRenderBridge::~ApplicationGuiRenderBridge()
{
    detach();
}

void ApplicationGuiRenderBridge::attach(
    render::GuiRenderHost& renderer,
    const render::RenderAssetStore& renderAssets,
    GuiRendererBackend& backend)
{
    if (!backend.ready()) throw std::logic_error("GUI bridge requires an initialized backend");
    detach();
    renderer_ = &renderer;
    renderAssets_ = &renderAssets;
    backend_ = &backend;

    if (renderer.hasViewport())
    {
        registerViewportTextures();
    }
}

void ApplicationGuiRenderBridge::detach() noexcept
{
    // Deferred RemoveTexture callbacks must run before ImGui destroys its pool.
    if (renderer_ != nullptr)
        renderer_->drainGuiResources();
    releasePreviewTextures();
    releaseViewportTextures();
    renderAssets_ = nullptr;
    renderer_ = nullptr;
    backend_ = nullptr;
}

void ApplicationGuiRenderBridge::resizeSceneViewport(
    uint32_t width,
    uint32_t height)
{
    if (renderer_ == nullptr || !renderer_->hasViewport() ||
        width == 0 || height == 0)
    {
        return;
    }

    const auto current =
        renderer_->viewportOutput(0);
    if (current.extent.width == width &&
        current.extent.height == height)
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
        renderer_->resizeViewport(width, height);
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
ApplicationGuiRenderBridge::currentFrame()
{
    if (renderer_ == nullptr)
    {
        return {};
    }
    render::ApplicationGuiRenderFrame frame{};
    if (!renderer_->hasViewport())
    {
        const auto extent = renderer_->outputExtent();
        frame.width = extent.width;
        frame.height = extent.height;
        return frame;
    }

    refreshViewportTexturesIfNeeded();
    const uint32_t frameIndex = renderer_->currentFrameIndex();
    if (frameIndex < viewportTextures_.size())
    {
        const auto output =
            renderer_->viewportOutput(frameIndex);
        frame.width = output.extent.width;
        frame.height = output.extent.height;
        frame.sceneViewport.textureId = viewportTextures_[frameIndex]->token();
    }
    return frame;
}

render::ApplicationGuiTexture ApplicationGuiRenderBridge::preview(
    asset::TextureAssetHandle texture)
{
    if (renderAssets_ == nullptr || !texture)
    {
        return {};
    }

    const auto* gpuTexture = renderAssets_->tryTexture(texture);
    const auto publication = renderAssets_->texturePublication(texture);
    const auto existing =
        std::find_if(previewTextures_.begin(), previewTextures_.end(),
                     [texture](const TextureEntry& entry) { return entry.texture == texture; });
    if (existing != previewTextures_.end() && gpuTexture &&
        existing->publication == publication)
    {
        return {existing->descriptor->token()};
    }
    if (!gpuTexture)
    {
        if (existing != previewTextures_.end())
        {
            renderer_->retireGuiResource(existing->descriptor);
            previewTextures_.erase(existing);
        }
        return {};
    }

    // Allocate the new registration first. Failure leaves the old entry intact;
    // after retirement admission, publishing the new owner cannot throw.
    auto descriptor = backend_->registerTexture(gpuTexture->view());
    const auto token = descriptor->token();
    if (existing != previewTextures_.end())
    {
        renderer_->retireGuiResource(existing->descriptor);
        existing->descriptor = std::move(descriptor);
        existing->publication = publication;
    }
    else
    {
        previewTextures_.push_back({texture, std::move(descriptor), publication});
    }
    return {token};
}

void ApplicationGuiRenderBridge::invalidatePreview(
    asset::TextureAssetHandle texture) noexcept
{
    // Explicit legacy invalidation is called only after GPU idle. Automatic
    // version refresh in preview() uses deferred ownership instead.
    const auto firstRemoved = std::remove_if(
        previewTextures_.begin(),
        previewTextures_.end(),
        [texture](const TextureEntry& entry)
        {
            if (entry.texture != texture)
            {
                return false;
            }
            return true;
        });
    previewTextures_.erase(firstRemoved, previewTextures_.end());
}

void ApplicationGuiRenderBridge::registerViewportTextures()
{
    if (renderer_ == nullptr || !renderer_->hasViewport() ||
        renderer_->frameCount() == 0)
    {
        return;
    }

    std::vector<GuiTextureRef> replacement;
    replacement.reserve(renderer_->frameCount());
    uint64_t revision = 0;
    for (uint32_t frameIndex = 0; frameIndex < renderer_->frameCount(); ++frameIndex)
    {
        const auto output = renderer_->viewportOutput(frameIndex);
        replacement.push_back(backend_->registerTexture(output.view));
        revision = output.revision;
    }
    // Keep old registrations and their views alive for any submitted UI draws.
    for (const auto& texture : viewportTextures_) renderer_->retireGuiResource(texture);
    viewportTextures_ = std::move(replacement);
    viewportTextureRevision_ = revision;
}

void ApplicationGuiRenderBridge::refreshViewportTexturesIfNeeded()
{
    if (renderer_ == nullptr || !renderer_->hasViewport() || renderer_->frameCount() == 0)
    {
        return;
    }

    const auto output =
        renderer_->viewportOutput(0);
    if (viewportTextures_.size() != renderer_->frameCount() ||
        viewportTextureRevision_ != output.revision)
    {
        registerViewportTextures();
    }
}

void ApplicationGuiRenderBridge::releaseViewportTextures() noexcept
{
    viewportTextures_.clear();
    viewportTextureRevision_ = 0;
}

void ApplicationGuiRenderBridge::releasePreviewTextures() noexcept
{
    previewTextures_.clear();
}

} // namespace rubia::integration::imgui
