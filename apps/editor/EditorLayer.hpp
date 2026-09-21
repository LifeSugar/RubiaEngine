#pragma once

#include "ApplicationGui.hpp"
#include "EditorSelection.hpp"
#include "panels/AssetBrowserPanel.hpp"
#include "panels/ConsolePanel.hpp"
#include "panels/InspectorPanel.hpp"
#include "panels/SceneHierarchyPanel.hpp"

#include <cstdint>

namespace rubia::editor
{

/// Editor-owned ImGui business layer. It does not own runtime or Vulkan state.
class EditorLayer final : public ApplicationGui
{
public:
    void update(const ApplicationGuiContext& context) override;
    void detach() noexcept override { assetController_.stop(); }
    [[nodiscard]] ApplicationGuiFrameOutput draw(
        const ApplicationGuiContext& context) override;

private:
    void drawDockSpace();
    [[nodiscard]] std::optional<float> drawSceneViewport(
        const ApplicationGuiContext& context);
    void drawRendererStats(const ApplicationGuiContext& context);

    EditorSelection selection_;
    EditorAssetController assetController_;
    bool browsed_ = false;
    SceneHierarchyPanel sceneHierarchyPanel_;
    InspectorPanel inspectorPanel_;
    AssetBrowserPanel assetBrowserPanel_;
    ConsolePanel consolePanel_;
    uint32_t sceneViewportWidth_ = 0;
    uint32_t sceneViewportHeight_ = 0;
    bool showSceneHierarchy_ = true;
    bool showInspector_ = true;
    bool showAssets_ = true;
    bool showSceneViewport_ = true;
    bool showRendererStats_ = true;
    bool showConsole_ = true;
    bool showPreferences_ = false;
};

} // namespace rubia::editor
