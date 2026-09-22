#include "inspectors/MaterialInspector.hpp"

#include "asset/AssetManager.hpp"
#include "render/ApplicationGuiRenderBridge.hpp"
#include "inspectors/InspectorWidgets.hpp"
#include "render/PipelineVariantKey.hpp"
#include "render/RenderQueue.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace rubia::editor
{

namespace
{

[[nodiscard]] const char* materialValueTypeName(
    asset::MaterialValueType type) noexcept
{
    switch (type)
    {
    case asset::MaterialValueType::Float: return "Float";
    case asset::MaterialValueType::Float2: return "Float2";
    case asset::MaterialValueType::Float3: return "Float3";
    case asset::MaterialValueType::Float4: return "Float4";
    case asset::MaterialValueType::Matrix4: return "Matrix4";
    case asset::MaterialValueType::Int: return "Int";
    case asset::MaterialValueType::UInt: return "UInt";
    case asset::MaterialValueType::Bool: return "Bool";
    }
    return "Unknown";
}

[[nodiscard]] const char* shaderStageName(asset::ShaderStage stage) noexcept
{
    switch (stage)
    {
    case asset::ShaderStage::Vertex: return "Vertex";
    case asset::ShaderStage::Fragment: return "Fragment";
    case asset::ShaderStage::Compute: return "Compute";
    }
    return "Unknown";
}

[[nodiscard]] const char* renderQueueName(render::RenderQueue queue) noexcept
{
    switch (queue)
    {
    case render::RenderQueue::Opaque: return "Opaque";
    case render::RenderQueue::AlphaClip: return "Alpha Clip";
    case render::RenderQueue::Transparent: return "Transparent";
    }
    return "Unknown";
}

[[nodiscard]] const char* depthCompareName(asset::DepthCompare compare) noexcept
{
    switch (compare)
    {
    case asset::DepthCompare::Never: return "Never";
    case asset::DepthCompare::Less: return "Less";
    case asset::DepthCompare::Equal: return "Equal";
    case asset::DepthCompare::LessEqual: return "Less Equal";
    case asset::DepthCompare::Greater: return "Greater";
    case asset::DepthCompare::NotEqual: return "Not Equal";
    case asset::DepthCompare::GreaterEqual: return "Greater Equal";
    case asset::DepthCompare::Always: return "Always";
    }
    return "Unknown";
}

[[nodiscard]] const char* blendModeName(render::PipelineBlendMode mode) noexcept
{
    switch (mode)
    {
    case render::PipelineBlendMode::Disabled: return "Disabled";
    case render::PipelineBlendMode::Alpha: return "Alpha";
    }
    return "Unknown";
}

[[nodiscard]] const char* cullModeName(render::PipelineCullMode mode) noexcept
{
    switch (mode)
    {
    case render::PipelineCullMode::None: return "None";
    case render::PipelineCullMode::Front: return "Front";
    case render::PipelineCullMode::Back: return "Back";
    }
    return "Unknown";
}

void drawMaterialParameterValue(
    const asset::MaterialParameterDesc& parameter,
    const std::vector<std::byte>& parameterData)
{
    const uint32_t valueSize =
        asset::MaterialTemplateAsset::valueSize(parameter.type);
    if (parameter.byteOffset > parameterData.size() ||
        valueSize > parameterData.size() - parameter.byteOffset)
    {
        ImGui::TextDisabled("Invalid parameter data");
        return;
    }

    const std::byte* source = parameterData.data() + parameter.byteOffset;
    switch (parameter.type)
    {
    case asset::MaterialValueType::Float:
    {
        float value = 0.0f;
        std::memcpy(&value, source, sizeof(value));
        ImGui::Text("%.3f", value);
        break;
    }
    case asset::MaterialValueType::Float2:
    case asset::MaterialValueType::Float3:
    case asset::MaterialValueType::Float4:
    {
        const uint32_t componentCount =
            static_cast<uint32_t>(parameter.type) -
            static_cast<uint32_t>(asset::MaterialValueType::Float2) + 2;
        std::array<float, 4> values{};
        std::memcpy(
            values.data(),
            source,
            componentCount * sizeof(float));
        if (componentCount == 2)
        {
            ImGui::Text("(%.3f, %.3f)", values[0], values[1]);
        }
        else if (componentCount == 3)
        {
            ImGui::Text(
                "(%.3f, %.3f, %.3f)",
                values[0],
                values[1],
                values[2]);
        }
        else
        {
            ImGui::Text(
                "(%.3f, %.3f, %.3f, %.3f)",
                values[0],
                values[1],
                values[2],
                values[3]);
        }
        break;
    }
    case asset::MaterialValueType::Matrix4:
    {
        std::array<float, 16> values{};
        std::memcpy(values.data(), source, sizeof(values));
        for (uint32_t row = 0; row < 4; ++row)
        {
            ImGui::Text(
                "[%.3f, %.3f, %.3f, %.3f]",
                values[row],
                values[4 + row],
                values[8 + row],
                values[12 + row]);
        }
        break;
    }
    case asset::MaterialValueType::Int:
    {
        int32_t value = 0;
        std::memcpy(&value, source, sizeof(value));
        ImGui::Text("%d", value);
        break;
    }
    case asset::MaterialValueType::UInt:
    {
        uint32_t value = 0;
        std::memcpy(&value, source, sizeof(value));
        ImGui::Text("%u", value);
        break;
    }
    case asset::MaterialValueType::Bool:
    {
        uint32_t value = 0;
        std::memcpy(&value, source, sizeof(value));
        ImGui::TextUnformatted(value != 0 ? "true" : "false");
        break;
    }
    }
}

struct MaterialTextureReimportBatch
{
    std::vector<importer::texture::TextureReimportRequest> requests;
    bool busy = false;
};

[[nodiscard]] MaterialTextureReimportBatch collectTextureReimports(
    const asset::MaterialAsset& material,
    const importer::texture::TextureImportRegistry* textureImports)
{
    MaterialTextureReimportBatch result{};
    if (textureImports == nullptr)
    {
        return result;
    }

    std::vector<asset::TextureAssetHandle> visited;
    for (asset::TextureAssetHandle texture : material.textures())
    {
        if (!texture ||
            std::find(visited.begin(), visited.end(), texture) !=
                visited.end())
        {
            continue;
        }
        visited.push_back(texture);

        const importer::texture::TextureImportRecord* record = textureImports->find(texture);
        if (record == nullptr)
        {
            continue;
        }
        result.busy = result.busy || record->reimporting;
        result.requests.push_back({texture, record->settings});
    }
    return result;
}

} // namespace

MaterialInspectorOutput MaterialInspector::drawMaterialAsset(
    const asset::AssetManager& assets,
    render::ApplicationGuiRenderBridge& texturePreviews,
    const importer::texture::TextureImportRegistry* textureImports,
    asset::MaterialAssetHandle target) const
{
    using namespace widgets;

    MaterialInspectorOutput output{};
    std::optional<InspectorTarget>& navigation = output.navigation;
    if (!assets.contains(target))
    {
        ImGui::TextDisabled("MaterialAsset selection is no longer valid");
        return output;
    }

    const asset::MaterialAsset& material = assets.material(target);
    ImGui::SeparatorText("Material Asset");
    drawProperty("Name", displayName(material.name(), "Unnamed Material"));

    const asset::MaterialTemplateAssetHandle templateHandle =
        material.materialTemplate();
    const asset::MaterialTemplateAsset* materialTemplate = nullptr;
    if (templateHandle && assets.contains(templateHandle))
    {
        materialTemplate = &assets.materialTemplate(templateHandle);
        if (drawReference(
                "Template",
                displayName(materialTemplate->name(), "Unnamed Template"),
                0))
        {
            navigation = InspectorTarget{templateHandle};
        }
    }
    else
    {
        drawProperty("Template", "Missing");
    }

    if (materialTemplate == nullptr)
    {
        ImGui::SeparatorText("Properties");
        ImGui::TextDisabled(
            "Properties unavailable because the template is missing");
        return output;
    }

    if (ImGui::CollapsingHeader("Shaders", ImGuiTreeNodeFlags_DefaultOpen))
    {
        drawProperty("Source", "Material Template");
        constexpr ImGuiTableFlags shaderTableFlags =
            ImGuiTableFlags_BordersInnerV |
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_SizingStretchProp;
        if (ImGui::BeginTable("##MaterialShaders", 3, shaderTableFlags))
        {
            ImGui::TableSetupColumn(
                "Stage",
                ImGuiTableColumnFlags_WidthFixed,
                72.0f);
            ImGui::TableSetupColumn(
                "Shader",
                ImGuiTableColumnFlags_WidthStretch,
                0.68f);
            ImGui::TableSetupColumn(
                "Entry",
                ImGuiTableColumnFlags_WidthStretch,
                0.32f);
            ImGui::TableHeadersRow();

            for (asset::ShaderAssetHandle shaderHandle : assets.shaderProgram(materialTemplate->program()).shaders())
            {
                ImGui::PushID(static_cast<int>(shaderHandle.index));
                ImGui::TableNextRow();
                if (shaderHandle && assets.contains(shaderHandle))
                {
                    const asset::ShaderAsset& shader = assets.shader(shaderHandle);
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(shaderStageName(shader.stage()));
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(
                        displayName(shader.name(), "Unnamed Shader"));
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextUnformatted(shader.entryPoint().c_str());
                }
                else
                {
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextDisabled("Unknown");
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextDisabled("Missing Shader");
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }

    const MaterialTextureReimportBatch reimportBatch =
        collectTextureReimports(material, textureImports);
    bool pipelineStateOpen = true;
    constexpr ImGuiTableFlags headerTableFlags =
        ImGuiTableFlags_SizingStretchProp |
        ImGuiTableFlags_NoSavedSettings;
    if (ImGui::BeginTable("##PipelineStateHeader", 2, headerTableFlags))
    {
        ImGui::TableSetupColumn(
            "##State",
            ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(
            "##Reimport",
            ImGuiTableColumnFlags_WidthFixed,
            94.0f);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        pipelineStateOpen = ImGui::CollapsingHeader(
            "Pipeline State",
            ImGuiTreeNodeFlags_DefaultOpen);
        ImGui::TableSetColumnIndex(1);

        const bool reimportDisabled =
            reimportBatch.requests.empty() || reimportBatch.busy;
        ImGui::BeginDisabled(reimportDisabled);
        if (ImGui::Button("Reimport All", ImVec2(-1.0f, 0.0f)))
        {
            output.textureReimports = reimportBatch.requests;
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            if (reimportBatch.requests.empty())
            {
                ImGui::SetTooltip(
                    "This material has no source-backed textures");
            }
            else if (reimportBatch.busy)
            {
                ImGui::SetTooltip(
                    "A material texture is already being reimported");
            }
            else
            {
                ImGui::SetTooltip(
                    "Cook and replace %zu textures sequentially",
                    reimportBatch.requests.size());
            }
        }
        ImGui::EndTable();
    }

    if (pipelineStateOpen)
    {
        const asset::MaterialRenderState& renderState = material.renderState();
        const render::PipelineVariantKey pipelineKey = render::makePipelineVariantKey(
            templateHandle,
            renderState);
        const render::RenderQueue queue = render::renderQueueFor(renderState);
        const std::string queueLabel =
            std::string(renderQueueName(queue)) + " (" +
            std::to_string(render::renderQueueValue(queue)) + ")";

        drawProperty(
            "Surface",
            renderState.transparent() ? "Transparent" : "Opaque");
        drawProperty("Render Queue", queueLabel.c_str());
        drawProperty("Blend", blendModeName(pipelineKey.blendMode));
        drawProperty("Cull", cullModeName(pipelineKey.cullMode));
        drawProperty(
            "Double Sided",
            renderState.doubleSided ? "Enabled" : "Disabled");
        drawProperty(
            "Depth Test",
            pipelineKey.depthTestEnabled ? "Enabled" : "Disabled");
        drawProperty(
            "Depth Write",
            pipelineKey.depthWriteEnabled ? "Enabled" : "Disabled");
        drawProperty(
            "Depth Compare",
            depthCompareName(pipelineKey.depthCompare));
        drawProperty(
            "Shader Feature",
            pipelineKey.shaderFeatures == render::ShaderFeatureFlags::AlphaClip
                ? "Alpha Clip"
                : "None");
        drawProperty(
            "Alpha Clip",
            renderState.alphaClipEnabled ? "Enabled" : "Disabled");
        if (renderState.alphaClipEnabled)
        {
            drawProperty("Alpha Threshold", renderState.alphaClipThreshold);
        }
    }

    if (!ImGui::CollapsingHeader(
            "Properties",
            ImGuiTreeNodeFlags_DefaultOpen))
    {
        return output;
    }

    constexpr ImGuiTableFlags tableFlags =
        ImGuiTableFlags_BordersInnerV |
        ImGuiTableFlags_RowBg |
        ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable("##MaterialProperties", 3, tableFlags))
    {
        return output;
    }

    ImGui::TableSetupColumn(
        "Property",
        ImGuiTableColumnFlags_WidthStretch,
        0.42f);
    ImGui::TableSetupColumn(
        "Value",
        ImGuiTableColumnFlags_WidthStretch,
        0.48f);
    ImGui::TableSetupColumn(
        "##Action",
        ImGuiTableColumnFlags_WidthFixed,
        54.0f);
    ImGui::TableHeadersRow();

    for (const asset::MaterialParameterDesc& parameter :
         materialTemplate->parameters())
    {
        ImGui::PushID(parameter.name.c_str());
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(parameter.name.c_str());
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(
                "%s%s\nByte offset: %u",
                materialValueTypeName(parameter.type),
                parameter.required ? " (required)" : "",
                parameter.byteOffset);
        }
        ImGui::TableSetColumnIndex(1);
        drawMaterialParameterValue(parameter, material.parameterData());
        ImGui::PopID();
    }

    const std::vector<asset::TextureAssetHandle>& textures = material.textures();
    for (const asset::MaterialTextureSlotDesc& slot :
         materialTemplate->textureSlots())
    {
        ImGui::PushID(slot.name.c_str());
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(slot.name.c_str());
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(
                "Texture%s\nSlot: %u\n"
                "Image: set %u, binding %u\n"
                "Sampler: set %u, binding %u",
                slot.required ? " (required)" : "",
                slot.slot,
                slot.imageBinding.set,
                slot.imageBinding.binding,
                slot.samplerBinding.set,
                slot.samplerBinding.binding);
        }

        const asset::TextureAssetHandle textureHandle =
            slot.slot < textures.size()
            ? textures[slot.slot]
            : asset::TextureAssetHandle{};
        ImGui::TableSetColumnIndex(1);
        if (textureHandle && assets.contains(textureHandle))
        {
            const asset::TextureAsset& texture = assets.texture(textureHandle);
            constexpr float thumbnailExtent = 36.0f;
            drawTextureImage(
                texturePreviews,
                textureHandle,
                texture,
                thumbnailExtent,
                thumbnailExtent);
            ImGui::SameLine();
            ImGui::TextUnformatted(
                displayName(texture.name(), "Unnamed Texture"));
            ImGui::TableSetColumnIndex(2);
            if (ImGui::SmallButton("Inspect"))
            {
                navigation = InspectorTarget{textureHandle};
            }
        }
        else
        {
            ImGui::TextDisabled("None");
        }
        ImGui::PopID();
    }

    ImGui::EndTable();
    return output;
}

std::optional<InspectorTarget> MaterialInspector::drawMaterialTemplate(
    const asset::AssetManager& assets,
    asset::MaterialTemplateAssetHandle target) const
{
    using namespace widgets;

    if (!assets.contains(target))
    {
        ImGui::TextDisabled(
            "MaterialTemplate selection is no longer valid");
        return std::nullopt;
    }

    const asset::MaterialTemplateAsset& materialTemplate =
        assets.materialTemplate(target);
    ImGui::SeparatorText("Material Template");
    drawProperty(
        "Name",
        displayName(materialTemplate.name(), "Unnamed Template"));
    drawProperty(
        "Parameters",
        static_cast<uint32_t>(materialTemplate.parameters().size()));
    drawProperty(
        "Texture Slots",
        static_cast<uint32_t>(materialTemplate.textureSlots().size()));
    drawProperty(
        "Shaders",
        static_cast<uint32_t>(assets.shaderProgram(materialTemplate.program()).shaders().size()));
    return std::nullopt;
}

} // namespace rubia::editor
