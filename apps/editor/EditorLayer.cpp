#include "EditorLayer.hpp"
#include "panels/PreferencesPanel.hpp"

#include "render/ApplicationGuiRenderBridge.hpp"
#include "scene/Scene.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <utility>

namespace rubia::editor
{

ApplicationGuiFrameOutput EditorLayer::draw(
    const ApplicationGuiContext& context)
{
    ApplicationGuiFrameOutput output{};
    drawDockSpace();
    sceneViewportWidth_ = 0;
    sceneViewportHeight_ = 0;

    if (showSceneHierarchy_)
    {
        sceneHierarchyPanel_.draw(
            context.scene,
            context.assets,
            selection_,
            &showSceneHierarchy_);
    }
    if (showInspector_)
    {
        output.textureReimports = inspectorPanel_.draw(
            context.scene,
            context.assets,
            context.render,
            context.textureImports,
            selection_,
            &showInspector_);
    }
    if (showAssets_)
    {
        std::vector<importer::texture::TextureReimportRequest> assetReimports =
            assetBrowserPanel_.draw(
                context.assets,
                context.textureImports,
                selection_,
                &showAssets_);
        output.textureReimports.insert(
            output.textureReimports.end(),
            std::make_move_iterator(assetReimports.begin()),
            std::make_move_iterator(assetReimports.end()));
    }
    if (showSceneViewport_)
    {
        output.sceneAspectRatio = drawSceneViewport(context);
    }
    if (showRendererStats_)
    {
        drawRendererStats(context);
    }
    if (showConsole_)
    {
        consolePanel_.draw(&showConsole_);
    }
    if (showPreferences_)
    {
        drawPreferencesPanel(&showPreferences_);
    }
    return output;
}

void EditorLayer::drawDockSpace()
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    constexpr ImGuiDockNodeFlags dockspaceFlags =
        ImGuiDockNodeFlags_PassthruCentralNode;
    ImGuiWindowFlags windowFlags =
        ImGuiWindowFlags_MenuBar |
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("EditorDockSpaceHost", nullptr, windowFlags);
    ImGui::PopStyleVar(3);

    bool resetLayout = false;
    if (ImGui::BeginMenuBar())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_CheckMark));
        const bool rubiaMenuOpen = ImGui::BeginMenu("RUBIA");
        ImGui::PopStyleColor();
        if (rubiaMenuOpen)
        {
            if (ImGui::MenuItem("Preference"))
            {
                showPreferences_ = true;
                ImGui::SetWindowFocus("Preference");
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View"))
        {
            ImGui::MenuItem(
                "Scene Hierarchy",
                nullptr,
                &showSceneHierarchy_);
            ImGui::MenuItem("Inspector", nullptr, &showInspector_);
            ImGui::MenuItem("Assets", nullptr, &showAssets_);
            ImGui::MenuItem("Scene Viewport", nullptr, &showSceneViewport_);
            ImGui::MenuItem("Renderer Stats", nullptr, &showRendererStats_);
            ImGui::MenuItem("Console", nullptr, &showConsole_);
            ImGui::Separator();
            resetLayout = ImGui::MenuItem("Reset Layout");
            ImGui::EndMenu();
        }
        const ImGuiIO& io = ImGui::GetIO();
        char performance[64];
        ImFormatString(performance, sizeof(performance), "%.0f FPS  /  %.2f ms",
            io.Framerate, io.Framerate > 0.0f ? 1000.0f / io.Framerate : 0.0f);
        const float statusX = ImGui::GetWindowWidth() -
            ImGui::CalcTextSize(performance).x - ImGui::GetStyle().FramePadding.x * 2.0f;
        if (statusX > ImGui::GetCursorPosX())
        {
            ImGui::SetCursorPosX(statusX);
            ImGui::TextDisabled("%s", performance);
        }
        ImGui::EndMenuBar();
    }

    // Version the default layout once; subsequent custom docking is persisted.
    const ImGuiID dockspaceId = ImGui::GetID("EditorMainDockSpaceV2");
    if (resetLayout)
    {
        ImGui::DockBuilderRemoveNode(dockspaceId);
        showSceneHierarchy_ = showInspector_ = showAssets_ = true;
        showSceneViewport_ = showRendererStats_ = showConsole_ = true;
    }
    if (ImGui::DockBuilderGetNode(dockspaceId) == nullptr)
    {
        ImGui::DockBuilderAddNode(
            dockspaceId,
            dockspaceFlags | ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetContentRegionAvail());

        ImGuiID center = dockspaceId;
        const ImGuiID right = ImGui::DockBuilderSplitNode(
            center,
            ImGuiDir_Right,
            0.26f,
            nullptr,
            &center);
        const ImGuiID bottom = ImGui::DockBuilderSplitNode(
            center,
            ImGuiDir_Down,
            0.28f,
            nullptr,
            &center);
        const ImGuiID left = ImGui::DockBuilderSplitNode(
            center,
            ImGuiDir_Left,
            0.24f,
            nullptr,
            &center);

        ImGui::DockBuilderDockWindow("Scene Hierarchy", left);
        ImGui::DockBuilderDockWindow("Inspector", right);
        ImGui::DockBuilderDockWindow("Assets", bottom);
        ImGui::DockBuilderDockWindow("Console", bottom);
        ImGui::DockBuilderDockWindow("Renderer Stats", bottom);
        ImGui::DockBuilderDockWindow("Scene Viewport", center);
        ImGui::DockBuilderFinish(dockspaceId);
    }
    ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), dockspaceFlags);
    ImGui::End();
}

std::optional<float> EditorLayer::drawSceneViewport(
    const ApplicationGuiContext& context)
{
    const bool visible = ImGui::Begin(
        "Scene Viewport",
        &showSceneViewport_,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (!visible)
    {
        ImGui::End();
        return std::nullopt;
    }

    ImGui::TextDisabled("SCENE");
    ImGui::SameLine();
    ImGui::TextUnformatted(context.scene.name().empty()
        ? "Untitled Scene" : context.scene.name().c_str());
    ImGui::Separator();
    const ImVec2 available = ImGui::GetContentRegionAvail();
    uint32_t renderWidth = 0;
    uint32_t renderHeight = 0;
    if (available.x > 0.0f && available.y > 0.0f)
    {
        const ImVec2 framebufferScale =
            ImGui::GetWindowViewport()->FramebufferScale;
        renderWidth = std::max(
            1u,
            static_cast<uint32_t>(
                available.x * framebufferScale.x + 0.5f));
        renderHeight = std::max(
            1u,
            static_cast<uint32_t>(
                available.y * framebufferScale.y + 0.5f));
        sceneViewportWidth_ = renderWidth;
        sceneViewportHeight_ = renderHeight;
        context.render.resizeSceneViewport(renderWidth, renderHeight);
    }
    const render::ApplicationGuiRenderFrame renderFrame =
        context.render.currentFrame();
    if (available.x > 0.0f && available.y > 0.0f &&
        renderFrame.sceneViewport)
    {
        const ImTextureID textureId = static_cast<ImTextureID>(
            renderFrame.sceneViewport.textureId);
        ImGui::Image(ImTextureRef(textureId), available);
    }
    else if (available.x > 0.0f && available.y > 0.0f)
    {
        const ImVec2 start = ImGui::GetCursorScreenPos();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(start,
            ImVec2(start.x + available.x, start.y + available.y),
            ImGui::GetColorU32(ImGuiCol_ChildBg), ImGui::GetStyle().ChildRounding);
        const char* message = "Scene preview unavailable";
        const ImVec2 textSize = ImGui::CalcTextSize(message);
        drawList->AddText(ImVec2(start.x + std::max(0.0f, (available.x - textSize.x) * 0.5f),
            start.y + std::max(0.0f, (available.y - textSize.y) * 0.5f)),
            ImGui::GetColorU32(ImGuiCol_TextDisabled), message);
        ImGui::Dummy(available);
    }
    ImGui::End();
    if (available.x <= 0.0f || available.y <= 0.0f)
    {
        return std::nullopt;
    }
    return static_cast<float>(renderWidth) /
        static_cast<float>(renderHeight);
}

void EditorLayer::drawRendererStats(
    const ApplicationGuiContext& context)
{
    ImGui::Begin("Renderer Stats", &showRendererStats_);
    static_cast<void>(context);
    const ImGuiIO& io = ImGui::GetIO();
    if (sceneViewportWidth_ > 0 && sceneViewportHeight_ > 0)
    {
        ImGui::Text(
            "Scene View: %u x %u",
            sceneViewportWidth_,
            sceneViewportHeight_);
    }
    else
    {
        ImGui::TextDisabled("Scene View: unavailable");
    }
    ImGui::Text(
        "Frame: %.3f ms (%.1f FPS)",
        io.Framerate > 0.0f ? 1000.0f / io.Framerate : 0.0f,
        io.Framerate);
    ImGui::End();
}

} // namespace rubia::editor
