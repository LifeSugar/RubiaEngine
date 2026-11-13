#include "RuntimeGui.hpp"

#include "render/ApplicationGuiRenderBridge.hpp"

#include <imgui.h>

namespace rubia::editor
{

ApplicationGuiFrameOutput RuntimeGui::draw(
    const ApplicationGuiContext& context)
{
    const render::ApplicationGuiRenderFrame renderFrame =
        context.render.currentFrame();
    const ImGuiIO& io = ImGui::GetIO();

    ImGui::SetNextWindowBgAlpha(0.85f);
    ImGui::Begin("Renderer");
    ImGui::Text(
        "Resolution: %u x %u",
        renderFrame.width,
        renderFrame.height);
    ImGui::Text(
        "Frame: %.3f ms (%.1f FPS)",
        io.Framerate > 0.0f ? 1000.0f / io.Framerate : 0.0f,
        io.Framerate);
    ImGui::TextUnformatted(context.contentLoading &&
        context.contentLoading->state != ContentLoadState::Ready
        ? "Draws: UI" : "Draws: scene + present + UI overlay");
    ImGui::End();
    return {};
}

} // namespace rubia::editor
