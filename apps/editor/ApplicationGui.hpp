#pragma once

#include "EditorFwd.hpp"
#include "content/ContentLoadStatus.hpp"
#include "texture/TextureImportRegistry.hpp"
#include "render/ResourcePreparation.hpp"

#include <optional>
#include <vector>

namespace rubia::editor
{

/// Non-owning services exposed to one application GUI frame.
struct ApplicationGuiContext
{
    asset::AssetManager& assets;
    scene::Scene& scene;
    render::ApplicationGuiRenderBridge& render;
    const importer::texture::TextureImportRegistry* textureImports = nullptr;
    const ContentLoadStatus* contentLoading = nullptr;
    // Optional non-owning port. Store tickets for status/cancel/release; the App
    // advances work before GUI/frame recording, never from draw().
    render::ResourcePreparation* preparations = nullptr;
};

/// Per-frame GUI decisions consumed before building the scene RenderFrame.
struct ApplicationGuiFrameOutput
{
    std::optional<float> sceneAspectRatio;
    std::vector<importer::texture::TextureReimportRequest> textureReimports;
};

/// UI business layer consumed by App without depending on Runtime or Editor UI.
class ApplicationGui
{
public:
    virtual ~ApplicationGui() = default;

    virtual void attach(const ApplicationGuiContext&) {}
    virtual void detach() noexcept {}
    [[nodiscard]] virtual ApplicationGuiFrameOutput draw(
        const ApplicationGuiContext& context) = 0;
};

} // namespace rubia::editor
