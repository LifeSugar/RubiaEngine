#pragma once

#include <array>
#include <cstddef>

namespace rubia::editor
{

enum class EditorThemePreset { Rubia, Midnight, Graphite, Count };
enum class EditorThemeColor
{
    Background, Panel, Control, Hover, Border, Accent, Selection, Text, MutedText, Count
};

struct EditorThemeSettings
{
    EditorThemePreset preset = EditorThemePreset::Rubia;
    std::array<std::array<float, 4>, static_cast<std::size_t>(EditorThemeColor::Count)> colors{};
};

[[nodiscard]] EditorThemeSettings makeEditorThemePreset(EditorThemePreset preset);
[[nodiscard]] const char* editorThemePresetName(EditorThemePreset preset);

// Initialize once before DPI scaling. Settings must outlive the ImGui context.
void applyEditorTheme(EditorThemeSettings& settings);
// Runtime changes only affect colors, preserving geometry and font scaling.
void applyEditorThemeColors(const EditorThemeSettings& settings);
[[nodiscard]] EditorThemeSettings* currentEditorThemeSettings();

} // namespace rubia::editor
