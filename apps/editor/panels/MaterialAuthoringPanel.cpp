#include "panels/MaterialAuthoringPanel.hpp"
#include <algorithm>
#include <cstring>
#include <imgui.h>
#include <type_traits>

namespace rubia::editor
{
namespace
{
void shaderCombo(const char *label, const asset::AssetManager &assets, asset::ShaderStage stage,
                 asset::ShaderAssetHandle &selected)
{
    if (!assets.contains(selected))
        selected = {};
    const char *preview = selected ? assets.shader(selected).name().c_str() : "Select shader";
    if (ImGui::BeginCombo(label, preview))
    {
        for (auto shader : assets.shaderHandles())
        {
            if (assets.shader(shader).stage() != stage)
                continue;
            ImGui::PushID(static_cast<int>(shader.index));
            if (ImGui::Selectable(assets.shader(shader).name().c_str(), shader == selected))
                selected = shader;
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
}
asset::MaterialValue initialValue(const asset::MaterialParameterDesc &parameter)
{
    switch (parameter.type)
    {
    case asset::MaterialValueType::Float:
        return parameter.name == "roughnessFactor" ? 1.0f : 0.0f;
    case asset::MaterialValueType::Float2:
        return glm::vec2(0);
    case asset::MaterialValueType::Float3:
        return glm::vec3(0);
    case asset::MaterialValueType::Float4:
        return glm::vec4(1);
    case asset::MaterialValueType::Matrix4:
        return glm::mat4(1);
    case asset::MaterialValueType::Int:
        return int32_t{0};
    case asset::MaterialValueType::UInt:
        return uint32_t{0};
    case asset::MaterialValueType::Bool:
        return false;
    }
    return 0.0f;
}
void editValue(const char *label, asset::MaterialValue &value)
{
    std::visit(
        [&](auto &data) {
            using T = std::decay_t<decltype(data)>;
            if constexpr (std::is_same_v<T, float>)
                ImGui::DragFloat(label, &data, 0.01f);
            else if constexpr (std::is_same_v<T, glm::vec2>)
                ImGui::DragFloat2(label, &data.x, 0.01f);
            else if constexpr (std::is_same_v<T, glm::vec3>)
                ImGui::DragFloat3(label, &data.x, 0.01f);
            else if constexpr (std::is_same_v<T, glm::vec4>)
                ImGui::DragFloat4(label, &data.x, 0.01f);
            else if constexpr (std::is_same_v<T, bool>)
                ImGui::Checkbox(label, &data);
            else if constexpr (std::is_same_v<T, int32_t>)
                ImGui::InputScalar(label, ImGuiDataType_S32, &data);
            else if constexpr (std::is_same_v<T, uint32_t>)
                ImGui::InputScalar(label, ImGuiDataType_U32, &data);
            else
            {
                ImGui::TextUnformatted(label);
                ImGui::PushID(label);
                for (int column = 0; column < 4; ++column)
                {
                    ImGui::PushID(column);
                    ImGui::DragFloat4("##Column", &data[column].x, 0.01f);
                    ImGui::PopID();
                }
                ImGui::PopID();
            }
        },
        value);
}
} // namespace
void MaterialAuthoringPanel::draw(const asset::AssetManager &assets,
                                  EditorAssetController &controller)
{
    if (domain_ != assets.domain())
    {
        vertex_ = {};
        fragment_ = {};
        program_ = {};
        template_ = {};
        slots_.clear();
        material_ = {};
        domain_ = assets.domain();
    }
    if (!ImGui::CollapsingHeader("Create Material"))
        return;
    shaderCombo("Vertex shader", assets, asset::ShaderStage::Vertex, vertex_);
    shaderCombo("Pixel shader", assets, asset::ShaderStage::Fragment, fragment_);
    ImGui::BeginDisabled(!vertex_ || !fragment_ || controller.busy());
    if (ImGui::Button("Build Program"))
        controller.buildProgram(vertex_, fragment_);
    ImGui::EndDisabled();
    if (program_ != controller.draftProgram())
    {
        program_ = controller.draftProgram();
        template_ = {};
        slots_.clear();
        if (assets.contains(program_))
        {
            const auto &bindings = assets.shaderProgram(program_).interface().bindings;
            for (const auto &binding : bindings)
            {
                if (binding.set != 1 || binding.type != asset::ShaderResourceType::SampledImage ||
                    binding.names.empty())
                    continue;
                asset::MaterialTextureSlotMetadata slot;
                slot.name = slot.imageResource = binding.names.front();
                // Only a UI default; the user can explicitly choose any reflected sampler.
                auto expected = slot.name;
                auto suffix = expected.rfind("Texture");
                if (suffix != std::string::npos && suffix + 7 == expected.size())
                    expected.replace(suffix, 7, "Sampler");
                for (const auto &sampler : bindings)
                    if (sampler.set == 1 && sampler.type == asset::ShaderResourceType::Sampler &&
                        std::find(sampler.names.begin(), sampler.names.end(), expected) !=
                            sampler.names.end())
                        slot.samplerResource = expected;
                slots_.push_back(std::move(slot));
            }
        }
    }
    if (!assets.contains(program_))
        return;
    for (auto &slot : slots_)
    {
        ImGui::PushID(slot.name.c_str());
        if (ImGui::BeginCombo(slot.imageResource.c_str(), slot.samplerResource.empty()
                                                              ? "Choose sampler"
                                                              : slot.samplerResource.c_str()))
        {
            for (const auto &binding : assets.shaderProgram(program_).interface().bindings)
                if (binding.set == 1 && binding.type == asset::ShaderResourceType::Sampler &&
                    !binding.names.empty())
                    if (ImGui::Selectable(binding.names.front().c_str(),
                                          binding.names.front() == slot.samplerResource))
                        slot.samplerResource = binding.names.front();
            ImGui::EndCombo();
        }
        ImGui::PopID();
    }
    ImGui::BeginDisabled(controller.busy());
    if (ImGui::Button("Generate Material Fields"))
    {
        asset::MaterialTemplateAsset::CreateInfo info;
        info.name = "Editor Material Template";
        info.program = program_;
        info.textureSlots = slots_;
        controller.buildTemplate(std::move(info));
    }
    ImGui::EndDisabled();
    if (template_ != controller.draftTemplate())
    {
        template_ = controller.draftTemplate();
        material_ = {};
        if (assets.contains(template_))
        {
            material_.materialTemplate = template_;
            for (const auto &parameter : assets.materialTemplate(template_).parameters())
                material_.parameters.push_back({parameter.name, initialValue(parameter)});
            for (const auto &slot : assets.materialTemplate(template_).textureSlots())
                material_.textures.push_back({slot.name, {}});
        }
    }
    if (!assets.contains(template_))
        return;
    ImGui::InputText("Name", name_.data(), name_.size());
    for (auto &parameter : material_.parameters)
        editValue(parameter.name.c_str(), parameter.value);
    for (auto &slot : material_.textures)
    {
        const auto texture = slot.texture;
        const char *preview =
            assets.contains(texture) ? assets.texture(texture).name().c_str() : "Select texture";
        if (ImGui::BeginCombo(slot.name.c_str(), preview))
        {
            for (auto handle : assets.textureHandles())
            {
                ImGui::PushID(static_cast<int>(handle.index));
                if (ImGui::Selectable(assets.texture(handle).name().c_str(), handle == texture))
                    slot.texture = handle;
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        if (ImGui::BeginDragDropTarget())
        {
            if (const auto *payload = ImGui::AcceptDragDropPayload(TexturePayload))
                if (payload->DataSize == sizeof(asset::TextureAssetHandle))
                    std::memcpy(&slot.texture, payload->Data, sizeof(slot.texture));
            ImGui::EndDragDropTarget();
        }
    }
    ImGui::Checkbox("Double sided", &material_.renderState.doubleSided);
    ImGui::Checkbox("Alpha clip", &material_.renderState.alphaClipEnabled);
    if (material_.renderState.alphaClipEnabled)
        ImGui::SliderFloat("Alpha threshold", &material_.renderState.alphaClipThreshold, 0, 1);
    bool missing = false;
    for (const auto &texture : material_.textures)
        missing |= !assets.contains(texture.texture);
    ImGui::BeginDisabled(controller.busy() || missing || name_[0] == '\0');
    if (ImGui::Button("Create Material Asset"))
    {
        material_.name = name_.data();
        controller.createMaterial(material_);
    }
    ImGui::EndDisabled();
    ImGui::TextWrapped(
        "Drag the material onto a scene instance to apply it. GLB materials remain unchanged.");
}
} // namespace rubia::editor
