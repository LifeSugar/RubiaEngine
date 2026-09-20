#include "panels/PreferencesPanel.hpp"

#include "EditorTheme.hpp"
#include "inspectors/InspectorWidgets.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>

namespace rubia::editor
{

void drawPreferencesPanel(bool* open)
{
    EditorThemeSettings* settings = currentEditorThemeSettings();
    if (settings == nullptr)
    {
        return;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float scale = ImGui::GetStyle().FontScaleDpi;
    ImGui::SetNextWindowSize(ImVec2(
        std::min(560.0f * scale, viewport->WorkSize.x),
        std::min(660.0f * scale, viewport->WorkSize.y)), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin("Preference", open, ImGuiWindowFlags_NoDocking))
    {
        ImGui::End();
        return;
    }

    bool changed = false;
    if (ImGui::BeginTabBar("##PreferenceSections"))
    {
        if (ImGui::BeginTabItem("Appearance"))
        {
            ImGui::Spacing();
            ImGui::TextWrapped(ImGui::GetIO().IniFilename
                ? "Changes apply immediately and are saved automatically."
                : "Changes apply immediately for this session.");
            ImGui::SeparatorText("Theme");
            if (widgets::beginPropertyRow("Preset", true))
            {
                if (ImGui::BeginCombo("##ThemePreset", editorThemePresetName(settings->preset)))
                {
                    for (int index = 0; index < static_cast<int>(EditorThemePreset::Count); ++index)
                    {
                        const auto preset = static_cast<EditorThemePreset>(index);
                        const bool selected = settings->preset == preset;
                        if (ImGui::Selectable(editorThemePresetName(preset), selected))
                        {
                            *settings = makeEditorThemePreset(preset);
                            changed = true;
                        }
                        if (selected)
                        {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
                widgets::endPropertyRow();
            }
            ImGui::TextDisabled("Selecting a preset replaces custom colors.");
            ImGui::SeparatorText("Editor colors");
            constexpr const char* labels[] = {
                "Background", "Panels", "Controls", "Hover", "Borders",
                "Accent", "Selection", "Text", "Muted text"};
            static_assert(sizeof(labels) / sizeof(labels[0]) ==
                static_cast<std::size_t>(EditorThemeColor::Count));
            for (std::size_t index = 0; index < settings->colors.size(); ++index)
            {
                if (widgets::beginPropertyRow(labels[index], true))
                {
                    changed |= ImGui::ColorEdit3("##Color", settings->colors[index].data(),
                        ImGuiColorEditFlags_DisplayHex | ImGuiColorEditFlags_NoOptions);
                    widgets::endPropertyRow();
                }
            }
            ImGui::Spacing();
            if (ImGui::Button("Reset colors", ImVec2(-1.0f, 0.0f)))
            {
                *settings = makeEditorThemePreset(settings->preset);
                changed = true;
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
    if (changed)
    {
        applyEditorThemeColors(*settings);
        ImGui::MarkIniSettingsDirty();
    }
}

} // namespace rubia::editor
