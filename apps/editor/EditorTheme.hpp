#pragma once

#include <array>
#include <cstddef>
#include <imgui.h>

namespace rubia::editor
{

enum class EditorThemePreset { Platinum, Slate, Parchment, Count };
enum class EditorThemeColor
{
    Background, Panel, Control, Hover, Border, Accent, Selection, Text, MutedText, Count
};

struct EditorThemeSettings
{
    EditorThemePreset preset = EditorThemePreset::Platinum;
    std::array<std::array<float, 4>, static_cast<std::size_t>(EditorThemeColor::Count)> colors{};
};

[[nodiscard]] EditorThemeSettings makeEditorThemePreset(EditorThemePreset preset);
[[nodiscard]] const char* editorThemePresetName(EditorThemePreset preset);

// Initialize once before DPI scaling. Settings must outlive the ImGui context.
void applyEditorTheme(EditorThemeSettings& settings);
// Runtime changes only affect colors, preserving geometry and font scaling.
void applyEditorThemeColors(const EditorThemeSettings& settings);
[[nodiscard]] EditorThemeSettings* currentEditorThemeSettings();

// These preserve native ImGui interaction, navigation and item IDs.
bool editorButton(const char* label, const ImVec2& size = ImVec2(0, 0));
bool beginEditorWindow(const char* title, bool* open = nullptr, ImGuiWindowFlags flags = 0);

} // namespace rubia::editor
