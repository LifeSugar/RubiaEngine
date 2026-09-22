#include "EditorTheme.hpp"

#include <imgui.h>

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace rubia::editor;

namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

struct Session
{
    EditorThemeSettings settings;
    ImGuiContext* context = ImGui::CreateContext();

    Session()
    {
        ImGui::GetIO().IniFilename = nullptr;
        applyEditorTheme(settings);
    }

    ~Session() { ImGui::DestroyContext(context); }
};

void checkPresetRoundTrip(EditorThemePreset preset)
{
    std::string saved;
    EditorThemeSettings expected = makeEditorThemePreset(preset);
    expected.colors[static_cast<std::size_t>(EditorThemeColor::Accent)] =
        {{0.137f, 0.521f, 0.913f, 1.0f}};
    expected.colors[static_cast<std::size_t>(EditorThemeColor::Panel)] =
        {{0.21f, 0.18f, 0.25f, 1.0f}};
    {
        Session session;
        session.settings = expected;
        auto& style = ImGui::GetStyle();
        style.ScaleAllSizes(1.5f);
        style.FontScaleDpi = 1.5f;
        const ImVec2 padding = style.FramePadding;
        const float rounding = style.FrameRounding;
        applyEditorThemeColors(session.settings);
        require(style.FramePadding.x == padding.x && style.FramePadding.y == padding.y &&
            style.FrameRounding == rounding && style.FontScaleDpi == 1.5f,
            "Changing colors must preserve DPI-scaled geometry");
        saved = ImGui::SaveIniSettingsToMemory();
    }
    {
        Session restored;
        ImGui::LoadIniSettingsFromMemory(saved.c_str());
        require(restored.settings.preset == expected.preset, "Preset did not persist");
        require(restored.settings.colors == expected.colors, "Custom colors did not persist");
        const auto& accent = ImGui::GetStyle().Colors[ImGuiCol_CheckMark];
        require(std::abs(accent.x - 0.137f) < 0.00001f &&
            std::abs(accent.z - 0.913f) < 0.00001f, "Restored colors were not applied");
        require(currentEditorThemeSettings() == &restored.settings,
            "Settings must belong to the current context");
    }
}
} // namespace

int main()
{
    try
    {
        for (int index = 0; index < static_cast<int>(EditorThemePreset::Count); ++index)
        {
            checkPresetRoundTrip(static_cast<EditorThemePreset>(index));
        }
        Session session;
        const auto defaults = makeEditorThemePreset(EditorThemePreset::Rubia);
        ImGui::LoadIniSettingsFromMemory(
            "[RubiaTheme][Appearance]\nPreset=999\n"
            "Color0=nan,0,0,1\nColor1=-1,0,0,1\nColor2=0,2,0,1\n"
            "Color3=0,0\nColor99=0,0,0,1\nColor4=0,0,0,1junk\n"
            "[RubiaTheme][Unknown]\nColor5=1,0,0,1\n");
        require(session.settings.colors == defaults.colors &&
            session.settings.preset == defaults.preset,
            "Invalid or unknown settings should retain safe defaults");
        session.settings = makeEditorThemePreset(EditorThemePreset::Midnight);
        ImGui::LoadIniSettingsFromMemory("[Window][Old Layout]\nPos=10,10\n");
        require(session.settings.colors == defaults.colors,
            "Existing layout files without appearance settings must use defaults");
        std::cout << "[OK] Theme persistence, invalid settings and DPI preservation passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
