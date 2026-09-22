#include "EditorTheme.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <cmath>
#include <cstdio>
#include <cstring>

namespace rubia::editor
{

namespace
{
constexpr const char* kSettingsType = "RubiaTheme";

void resetSettings(ImGuiContext*, ImGuiSettingsHandler* handler)
{
    *static_cast<EditorThemeSettings*>(handler->UserData) =
        makeEditorThemePreset(EditorThemePreset::Rubia);
}

void* openSettings(ImGuiContext*, ImGuiSettingsHandler* handler, const char* name)
{
    return std::strcmp(name, "Appearance") == 0 ? handler->UserData : nullptr;
}

void readSettings(ImGuiContext*, ImGuiSettingsHandler*, void* entry, const char* line)
{
    auto& settings = *static_cast<EditorThemeSettings*>(entry);
    int preset = -1;
    int consumed = 0;
    if (std::sscanf(line, "Preset=%d%n", &preset, &consumed) == 1 &&
        line[consumed] == '\0' && preset >= 0 &&
        preset < static_cast<int>(EditorThemePreset::Count))
    {
        settings = makeEditorThemePreset(static_cast<EditorThemePreset>(preset));
        return;
    }

    unsigned index = 0;
    std::array<float, 4> color{};
    consumed = 0;
    if (std::sscanf(line, "Color%u=%f,%f,%f,%f%n", &index,
            &color[0], &color[1], &color[2], &color[3], &consumed) != 5 ||
        line[consumed] != '\0' || index >= settings.colors.size())
    {
        return;
    }
    for (float component : color)
    {
        if (!std::isfinite(component) || component < 0.0f || component > 1.0f)
        {
            return;
        }
    }
    settings.colors[index] = color;
}

void applySettings(ImGuiContext*, ImGuiSettingsHandler* handler)
{
    applyEditorThemeColors(*static_cast<EditorThemeSettings*>(handler->UserData));
}

void writeSettings(ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* output)
{
    const auto& settings = *static_cast<EditorThemeSettings*>(handler->UserData);
    output->appendf("[%s][Appearance]\nPreset=%d\n", kSettingsType,
        static_cast<int>(settings.preset));
    for (std::size_t index = 0; index < settings.colors.size(); ++index)
    {
        const auto& color = settings.colors[index];
        output->appendf("Color%u=%.9g,%.9g,%.9g,%.9g\n", static_cast<unsigned>(index),
            color[0], color[1], color[2], color[3]);
    }
    output->append("\n");
}
} // namespace

EditorThemeSettings makeEditorThemePreset(EditorThemePreset preset)
{
    EditorThemeSettings result;
    result.colors = {{
        {{0.075f, 0.082f, 0.102f, 1.0f}},
        {{0.105f, 0.114f, 0.137f, 1.0f}},
        {{0.153f, 0.165f, 0.196f, 1.0f}},
        {{0.208f, 0.220f, 0.267f, 1.0f}},
        {{0.235f, 0.247f, 0.294f, 0.55f}},
        {{0.655f, 0.608f, 0.961f, 1.0f}},
        {{0.286f, 0.259f, 0.427f, 1.0f}},
        {{0.89f, 0.90f, 0.93f, 1.0f}},
        {{0.53f, 0.56f, 0.63f, 1.0f}}
    }};
    if (preset == EditorThemePreset::Midnight)
    {
        result.preset = preset;
        result.colors = {{
            {{0.055f, 0.075f, 0.110f, 1.0f}},
            {{0.080f, 0.110f, 0.155f, 1.0f}},
            {{0.120f, 0.165f, 0.220f, 1.0f}},
            {{0.170f, 0.235f, 0.310f, 1.0f}},
            {{0.260f, 0.350f, 0.440f, 0.55f}},
            {{0.380f, 0.720f, 0.980f, 1.0f}},
            {{0.140f, 0.290f, 0.420f, 1.0f}},
            {{0.87f, 0.92f, 0.97f, 1.0f}},
            {{0.51f, 0.61f, 0.72f, 1.0f}}
        }};
    }
    else if (preset == EditorThemePreset::Graphite)
    {
        result.preset = preset;
        result.colors = {{
            {{0.080f, 0.080f, 0.085f, 1.0f}},
            {{0.115f, 0.115f, 0.120f, 1.0f}},
            {{0.170f, 0.170f, 0.180f, 1.0f}},
            {{0.235f, 0.235f, 0.250f, 1.0f}},
            {{0.330f, 0.330f, 0.350f, 0.55f}},
            {{0.780f, 0.800f, 0.840f, 1.0f}},
            {{0.290f, 0.300f, 0.330f, 1.0f}},
            {{0.91f, 0.91f, 0.92f, 1.0f}},
            {{0.57f, 0.57f, 0.60f, 1.0f}}
        }};
    }
    return result;
}

const char* editorThemePresetName(EditorThemePreset preset)
{
    switch (preset)
    {
    case EditorThemePreset::Rubia: return "Rubia Violet";
    case EditorThemePreset::Midnight: return "Midnight Blue";
    case EditorThemePreset::Graphite: return "Graphite";
    default: return "Rubia Violet";
    }
}

EditorThemeSettings* currentEditorThemeSettings()
{
    ImGuiSettingsHandler* handler = ImGui::FindSettingsHandler(kSettingsType);
    return handler ? static_cast<EditorThemeSettings*>(handler->UserData) : nullptr;
}

void applyEditorTheme(EditorThemeSettings& settings)
{
    settings = makeEditorThemePreset(EditorThemePreset::Rubia);
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(14.0f, 12.0f);
    style.FramePadding = ImVec2(10.0f, 6.0f);
    style.ItemSpacing = ImVec2(10.0f, 8.0f);
    style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
    style.CellPadding = ImVec2(8.0f, 5.0f);
    style.IndentSpacing = 18.0f;
    style.ScrollbarSize = 12.0f;
    style.GrabMinSize = 10.0f;
    style.WindowRounding = 8.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 5.0f;
    style.PopupRounding = 7.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 5.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 0.0f;
    style.FrameBorderSize = 0.0f;
    style.PopupBorderSize = 1.0f;
    style.TabBorderSize = 0.0f;
    style.TabBarBorderSize = 0.0f;
    style.TabBarOverlineSize = 2.0f;
    style.DockingSeparatorSize = 5.0f;
    style.SeparatorTextBorderSize = 1.0f;
    style.SeparatorTextPadding = ImVec2(0.0f, 10.0f);
    style.WindowMenuButtonPosition = ImGuiDir_None;
    style.TreeLinesFlags = ImGuiTreeNodeFlags_DrawLinesToNodes;

    applyEditorThemeColors(settings);
    ImGuiSettingsHandler handler;
    handler.TypeName = kSettingsType;
    handler.TypeHash = ImHashStr(kSettingsType);
    handler.UserData = &settings;
    handler.ClearAllFn = resetSettings;
    handler.ReadInitFn = resetSettings;
    handler.ReadOpenFn = openSettings;
    handler.ReadLineFn = readSettings;
    handler.ApplyAllFn = applySettings;
    handler.WriteAllFn = writeSettings;
    ImGui::AddSettingsHandler(&handler);
}

void applyEditorThemeColors(const EditorThemeSettings& settings)
{
    const auto getColor = [&](EditorThemeColor role)
    {
        const auto& value = settings.colors[static_cast<std::size_t>(role)];
        return ImVec4(value[0], value[1], value[2], value[3]);
    };
    const ImVec4 base = getColor(EditorThemeColor::Background);
    const ImVec4 panel = getColor(EditorThemeColor::Panel);
    const ImVec4 raised = getColor(EditorThemeColor::Control);
    const ImVec4 hover = getColor(EditorThemeColor::Hover);
    const ImVec4 border = getColor(EditorThemeColor::Border);
    const ImVec4 accent = getColor(EditorThemeColor::Accent);
    const ImVec4 selection = getColor(EditorThemeColor::Selection);
    auto& colors = ImGui::GetStyle().Colors;
    colors[ImGuiCol_Text] = getColor(EditorThemeColor::Text);
    colors[ImGuiCol_TextDisabled] = getColor(EditorThemeColor::MutedText);
    colors[ImGuiCol_WindowBg] = panel;
    colors[ImGuiCol_ChildBg] = base;
    colors[ImGuiCol_PopupBg] = panel;
    colors[ImGuiCol_Border] = border;
    colors[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_FrameBg] = raised;
    colors[ImGuiCol_FrameBgHovered] = hover;
    colors[ImGuiCol_FrameBgActive] = selection;
    colors[ImGuiCol_TitleBg] = base;
    colors[ImGuiCol_TitleBgActive] = raised;
    colors[ImGuiCol_TitleBgCollapsed] = base;
    colors[ImGuiCol_MenuBarBg] = base;
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_ScrollbarGrab] = raised;
    colors[ImGuiCol_ScrollbarGrabHovered] = hover;
    colors[ImGuiCol_ScrollbarGrabActive] = accent;
    colors[ImGuiCol_CheckMark] = accent;
    colors[ImGuiCol_SliderGrab] = accent;
    colors[ImGuiCol_SliderGrabActive] = ImLerp(accent, ImVec4(1, 1, 1, 1), 0.20f);
    colors[ImGuiCol_Button] = raised;
    colors[ImGuiCol_ButtonHovered] = hover;
    colors[ImGuiCol_ButtonActive] = selection;
    colors[ImGuiCol_Header] = selection;
    colors[ImGuiCol_HeaderHovered] = hover;
    colors[ImGuiCol_HeaderActive] = selection;
    colors[ImGuiCol_Separator] = border;
    colors[ImGuiCol_SeparatorHovered] = accent;
    colors[ImGuiCol_SeparatorActive] = accent;
    colors[ImGuiCol_ResizeGrip] = ImVec4(accent.x, accent.y, accent.z, 0.15f);
    colors[ImGuiCol_ResizeGripHovered] = accent;
    colors[ImGuiCol_ResizeGripActive] = accent;
    colors[ImGuiCol_Tab] = base;
    colors[ImGuiCol_TabHovered] = hover;
    colors[ImGuiCol_TabSelected] = raised;
    colors[ImGuiCol_TabSelectedOverline] = accent;
    colors[ImGuiCol_TabDimmed] = base;
    colors[ImGuiCol_TabDimmedSelected] = panel;
    colors[ImGuiCol_TabDimmedSelectedOverline] = selection;
    colors[ImGuiCol_DockingPreview] = ImVec4(accent.x, accent.y, accent.z, 0.30f);
    colors[ImGuiCol_DockingEmptyBg] = base;
    colors[ImGuiCol_TableHeaderBg] = raised;
    colors[ImGuiCol_TableBorderStrong] = border;
    colors[ImGuiCol_TableBorderLight] = border;
    colors[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.025f);
    colors[ImGuiCol_TextSelectedBg] = selection;
    colors[ImGuiCol_NavCursor] = accent;
    colors[ImGuiCol_TreeLines] = border;
}

} // namespace rubia::editor
