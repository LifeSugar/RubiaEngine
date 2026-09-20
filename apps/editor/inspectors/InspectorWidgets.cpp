#include "inspectors/InspectorWidgets.hpp"

#include "asset/TextureAsset.hpp"
#include "render/ApplicationGuiRenderBridge.hpp"

#include <imgui.h>

#include <cstdio>

namespace rubia::editor::widgets
{

const char* displayName(
    const std::string& name,
    const char* fallback) noexcept
{
    return name.empty() ? fallback : name.c_str();
}

bool beginPropertyRow(const char* label, bool framed)
{
    ImGui::PushID(label);
    if (!ImGui::BeginTable("##Property", 2,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
    {
        ImGui::PopID();
        return false;
    }
    ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, 0.40f);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.60f);
    ImGui::TableNextColumn();
    if (framed)
    {
        ImGui::AlignTextToFramePadding();
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", label);
    ImGui::PopStyleColor();
    ImGui::TableNextColumn();
    if (framed)
    {
        ImGui::SetNextItemWidth(-1.0f);
    }
    return true;
}

void endPropertyRow()
{
    ImGui::EndTable();
    ImGui::PopID();
}

void drawProperty(const char* label, const char* value)
{
    if (beginPropertyRow(label))
    {
        ImGui::TextWrapped("%s", value);
        endPropertyRow();
    }
}

void drawProperty(const char* label, uint32_t value)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%u", value);
    drawProperty(label, text);
}

void drawProperty(const char* label, float value)
{
    char text[64];
    std::snprintf(text, sizeof(text), "%.3f", value);
    drawProperty(label, text);
}

bool drawReference(const char* label, const char* value, int id)
{
    ImGui::PushID(id);
    drawProperty(label, value);
    const bool clicked = ImGui::Button("Inspect asset", ImVec2(-1.0f, 0.0f));
    ImGui::PopID();
    return clicked;
}

void drawTextureImage(
    render::ApplicationGuiRenderBridge& texturePreviews,
    asset::TextureAssetHandle handle,
    const asset::TextureAsset& texture,
    float maxWidth,
    float maxHeight)
{
    if (texture.width() == 0 || texture.height() == 0 ||
        maxWidth <= 0.0f || maxHeight <= 0.0f)
    {
        ImGui::TextDisabled("Preview unavailable");
        return;
    }

    const render::ApplicationGuiTexture preview = texturePreviews.preview(handle);
    if (!preview)
    {
        ImGui::TextDisabled("Preview unavailable");
        return;
    }

    const float aspect =
        static_cast<float>(texture.width()) /
        static_cast<float>(texture.height());
    ImVec2 size{maxWidth, maxWidth / aspect};
    if (size.y > maxHeight)
    {
        size.y = maxHeight;
        size.x = maxHeight * aspect;
    }

    const ImTextureRef textureReference(
        static_cast<ImTextureID>(preview.textureId));
    ImGui::ImageWithBg(
        textureReference,
        size,
        ImVec2(0.0f, 0.0f),
        ImVec2(1.0f, 1.0f),
        ImVec4(0.16f, 0.16f, 0.16f, 1.0f));
}

} // namespace rubia::editor::widgets
