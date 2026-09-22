#include "panels/TransformPanel.hpp"
#include "inspectors/InspectorWidgets.hpp"

#include <imgui.h>

#include <glm/common.hpp>
#include <glm/gtc/quaternion.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>

#include <cmath>

namespace rubia::editor
{
namespace
{

[[nodiscard]] float cleanDisplayValue(float value) noexcept
{
    return std::abs(value) < 0.00005f ? 0.0f : value;
}

void drawVectorRow(const char* label, const glm::vec3& value)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1);
    ImGui::Text("%.3f", cleanDisplayValue(value.x));
    ImGui::TableSetColumnIndex(2);
    ImGui::Text("%.3f", cleanDisplayValue(value.y));
    ImGui::TableSetColumnIndex(3);
    ImGui::Text("%.3f", cleanDisplayValue(value.z));
}

} // namespace

void TransformPanel::draw(
    const glm::mat4& transform,
    Space space) const
{
    if (!ImGui::CollapsingHeader(
            "Transform",
            ImGuiTreeNodeFlags_DefaultOpen))
    {
        return;
    }

    widgets::drawProperty("Space",
        space == Space::World ? "World" : "Local (Parent)");

    glm::vec3 scale{};
    glm::quat rotation{};
    glm::vec3 translation{};
    glm::vec3 skew{};
    glm::vec4 perspective{};
    if (!glm::decompose(
            transform,
            scale,
            rotation,
            translation,
            skew,
            perspective))
    {
        ImGui::TextDisabled("Transform matrix cannot be decomposed");
        return;
    }

    rotation = glm::normalize(rotation);
    const glm::vec3 rotationDegrees = glm::degrees(
        glm::eulerAngles(rotation));

    constexpr ImGuiTableFlags tableFlags =
        ImGuiTableFlags_BordersInnerV |
        ImGuiTableFlags_RowBg |
        ImGuiTableFlags_SizingStretchSame;
    if (ImGui::BeginTable("##TransformValues", 4, tableFlags))
    {
        ImGui::TableSetupColumn(
            "Component",
            ImGuiTableColumnFlags_WidthStretch,
            1.6f);
        ImGui::TableSetupColumn("X");
        ImGui::TableSetupColumn("Y");
        ImGui::TableSetupColumn("Z");
        ImGui::TableHeadersRow();

        drawVectorRow("Translation", translation);
        drawVectorRow("Rotation (deg)", rotationDegrees);
        drawVectorRow("Scale", scale);
        ImGui::EndTable();
    }

    constexpr float epsilon = 0.0001f;
    if (glm::any(glm::greaterThan(glm::abs(skew), glm::vec3(epsilon))))
    {
        ImGui::TextDisabled(
            "Matrix contains skew; TRS is an approximate view");
    }
}

} // namespace rubia::editor
