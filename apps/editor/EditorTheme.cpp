#include "EditorTheme.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>

namespace rubia::editor
{

namespace
{
// Version appearance separately from docking: old dark palettes must not override Platinum.
constexpr const char* kSettingsType = "RubiaPlatinumTheme";

void resetSettings(ImGuiContext*, ImGuiSettingsHandler* handler)
{
    *static_cast<EditorThemeSettings*>(handler->UserData) =
        makeEditorThemePreset(EditorThemePreset::Platinum);
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
        {{0.95f, 0.95f, 0.93f, 1.0f}}, // Paper / content wells
        {{0.80f, 0.80f, 0.78f, 1.0f}}, // Platinum window chrome
        {{0.87f, 0.87f, 0.85f, 1.0f}}, // Raised controls
        {{0.92f, 0.92f, 0.90f, 1.0f}},
        {{0.43f, 0.44f, 0.43f, 1.0f}},
        {{0.29f, 0.36f, 0.41f, 1.0f}}, // Graphite blue
        {{0.67f, 0.74f, 0.78f, 1.0f}},
        {{0.12f, 0.13f, 0.13f, 1.0f}},
        {{0.35f, 0.36f, 0.35f, 1.0f}}
    }};
    if (preset == EditorThemePreset::Slate)
    {
        result.preset = preset;
        result.colors[static_cast<std::size_t>(EditorThemeColor::Panel)] = {{0.75f, 0.78f, 0.80f, 1.0f}};
        result.colors[static_cast<std::size_t>(EditorThemeColor::Control)] = {{0.84f, 0.87f, 0.89f, 1.0f}};
    }
    else if (preset == EditorThemePreset::Parchment)
    {
        result.preset = preset;
        result.colors[static_cast<std::size_t>(EditorThemeColor::Panel)] = {{0.83f, 0.81f, 0.76f, 1.0f}};
        result.colors[static_cast<std::size_t>(EditorThemeColor::Background)] = {{0.97f, 0.96f, 0.91f, 1.0f}};
        result.colors[static_cast<std::size_t>(EditorThemeColor::Control)] = {{0.90f, 0.88f, 0.82f, 1.0f}};
        result.colors[static_cast<std::size_t>(EditorThemeColor::Selection)] = {{0.77f, 0.76f, 0.65f, 1.0f}};
    }
    return result;
}

const char* editorThemePresetName(EditorThemePreset preset)
{
    switch (preset)
    {
    case EditorThemePreset::Platinum: return "Platinum";
    case EditorThemePreset::Slate: return "Slate";
    case EditorThemePreset::Parchment: return "Parchment";
    default: return "Platinum";
    }
}

EditorThemeSettings* currentEditorThemeSettings()
{
    ImGuiSettingsHandler* handler = ImGui::FindSettingsHandler(kSettingsType);
    return handler ? static_cast<EditorThemeSettings*>(handler->UserData) : nullptr;
}

void applyEditorTheme(EditorThemeSettings& settings)
{
    settings = makeEditorThemePreset(EditorThemePreset::Platinum);
    ImGui::StyleColorsLight();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(10.0f, 10.0f);
    style.FramePadding = ImVec2(8.0f, 4.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
    style.CellPadding = ImVec2(6.0f, 4.0f);
    style.IndentSpacing = 18.0f;
    style.ScrollbarSize = 15.0f;
    style.GrabMinSize = 10.0f;
    style.WindowRounding = 0.0f;
    style.ChildRounding = 0.0f;
    style.FrameRounding = 1.0f;
    style.PopupRounding = 0.0f;
    style.ScrollbarRounding = 0.0f;
    style.GrabRounding = 0.0f;
    style.TabRounding = 1.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.TabBorderSize = 1.0f;
    style.TabBarBorderSize = 1.0f;
    style.TabBarOverlineSize = 0.0f;
    style.DockingSeparatorSize = 5.0f;
    style.SeparatorTextBorderSize = 1.0f;
    style.SeparatorTextPadding = ImVec2(0.0f, 10.0f);
    style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
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
    colors[ImGuiCol_FrameBg] = base;
    colors[ImGuiCol_FrameBgHovered] = hover;
    colors[ImGuiCol_FrameBgActive] = selection;
    colors[ImGuiCol_TitleBg] = panel;
    colors[ImGuiCol_TitleBgActive] = raised;
    colors[ImGuiCol_TitleBgCollapsed] = base;
    colors[ImGuiCol_MenuBarBg] = raised;
    colors[ImGuiCol_ScrollbarBg] = panel;
    colors[ImGuiCol_ScrollbarGrab] = border;
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
    colors[ImGuiCol_Tab] = panel;
    colors[ImGuiCol_TabHovered] = hover;
    colors[ImGuiCol_TabSelected] = base;
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
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(0, 0, 0, 0.035f);
    colors[ImGuiCol_TextSelectedBg] = selection;
    colors[ImGuiCol_NavCursor] = accent;
    colors[ImGuiCol_TreeLines] = border;
}

bool editorButton(const char* label, const ImVec2& size)
{
    if (ImGui::GetCurrentWindow()->SkipItems)
        return false;
    const bool clicked = ImGui::Button(label, size);
    if (!ImGui::IsItemVisible())
        return clicked;

    // Paint only the inner bevel; Button owns input, focus, tooltips and navigation.
    const bool pressed = ImGui::IsItemActive();
    const float pixel = std::max(1.0f, ImGui::GetStyle().FontScaleDpi);
    ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    a.x += pixel; a.y += pixel;
    b.x -= pixel * 2; b.y -= pixel * 2;
    if (b.x <= a.x || b.y <= a.y)
        return clicked;
    const ImVec4 face = ImGui::GetStyleColorVec4(ImGuiCol_Button);
    const ImU32 light = ImGui::GetColorU32(ImLerp(face, ImVec4(1, 1, 1, 1), 0.8f));
    const ImU32 dark = ImGui::GetColorU32(ImLerp(face, ImVec4(0, 0, 0, 1), 0.35f));
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddLine(a, ImVec2(b.x, a.y), pressed ? dark : light, pixel);
    draw->AddLine(a, ImVec2(a.x, b.y), pressed ? dark : light, pixel);
    draw->AddLine(ImVec2(a.x, b.y), b, pressed ? light : dark, pixel);
    draw->AddLine(ImVec2(b.x, a.y), b, pressed ? light : dark, pixel);
    return clicked;
}

bool beginEditorWindow(const char* title, bool* open, ImGuiWindowFlags flags)
{
    const bool visible = ImGui::Begin(title, open, flags);
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (!visible || (flags & ImGuiWindowFlags_NoTitleBar))
        return visible;

    ImRect bar = window->TitleBarRect();
    const float pixel = std::max(1.0f, ImGui::GetStyle().FontScaleDpi);
    float start = bar.Min.x + 24.0f * pixel;
    float end = bar.Max.x - 24.0f * pixel;
    if (window->DockIsActive)
    {
        // Only the visible tab paints, and only in the unused tail of the tab bar.
        if (!window->DockTabIsVisible || !window->DockNode || !window->DockNode->TabBar)
            return visible;
        const ImGuiTabBar* tabs = window->DockNode->TabBar;
        bar = tabs->BarRect;
        start = bar.Min.x + tabs->WidthAllTabs + 12.0f * pixel;
        end = bar.Max.x - 8.0f * pixel;
    }
    if (bar.GetHeight() <= 0 || end - start < 24.0f * pixel)
        return visible;

    const float titleWidth = ImGui::CalcTextSize(title, nullptr, true).x;
    const float titleLeft = (bar.Min.x + bar.Max.x - titleWidth) * 0.5f - 8.0f * pixel;
    const float titleRight = titleLeft + titleWidth + 16.0f * pixel;
    ImDrawList* draw = window->DrawList;
    draw->PushClipRect(bar.Min, bar.Max, false);
    const ImVec4 face = ImGui::GetStyleColorVec4(ImGuiCol_TitleBgActive);
    const ImU32 dark = ImGui::GetColorU32(ImLerp(face, ImVec4(0, 0, 0, 1), 0.18f));
    const ImU32 light = ImGui::GetColorU32(ImLerp(face, ImVec4(1, 1, 1, 1), 0.65f));
    const auto stripe = [&](float left, float right, float y)
    {
        if (right <= left) return;
        draw->AddLine(ImVec2(left, y), ImVec2(right, y), dark, pixel);
        draw->AddLine(ImVec2(left, y + pixel), ImVec2(right, y + pixel), light, pixel);
    };
    for (float y = bar.Min.y + 6.0f * pixel; y < bar.Max.y - 4.0f * pixel; y += 4.0f * pixel)
    {
        if (window->DockIsActive)
            stripe(start, end, y);
        else
        {
            stripe(start, std::min(end, titleLeft), y);
            stripe(std::max(start, titleRight), end, y);
        }
    }
    draw->PopClipRect();
    return visible;
}

} // namespace rubia::editor
