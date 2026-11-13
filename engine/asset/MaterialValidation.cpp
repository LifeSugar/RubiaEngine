#include "asset/MaterialValidation.hpp"

#include "asset/AssetManager.hpp"
#include "asset/MaterialTemplateBuilder.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace rubia::asset
{
namespace
{

[[nodiscard]] MaterialValueType materialValueType(
    const MaterialValue& value)
{
    switch (value.index())
    {
    case 0: return MaterialValueType::Float;
    case 1: return MaterialValueType::Float2;
    case 2: return MaterialValueType::Float3;
    case 3: return MaterialValueType::Float4;
    case 4: return MaterialValueType::Matrix4;
    case 5: return MaterialValueType::Int;
    case 6: return MaterialValueType::UInt;
    case 7: return MaterialValueType::Bool;
    default: return MaterialValueType::Float;
    }
}

[[nodiscard]] bool finiteMaterialValue(const MaterialValue& value)
{
    return std::visit(
        [](const auto& typedValue)
        {
            using Value = std::decay_t<decltype(typedValue)>;
            if constexpr (std::is_floating_point_v<Value>)
            {
                return std::isfinite(typedValue);
            }
            else if constexpr (
                std::is_same_v<Value, glm::vec2> ||
                std::is_same_v<Value, glm::vec3> ||
                std::is_same_v<Value, glm::vec4>)
            {
                for (glm::length_t index = 0;
                     index < typedValue.length();
                     ++index)
                {
                    if (!std::isfinite(typedValue[index]))
                    {
                        return false;
                    }
                }
                return true;
            }
            else if constexpr (std::is_same_v<Value, glm::mat4>)
            {
                for (glm::length_t column = 0; column < 4; ++column)
                {
                    for (glm::length_t row = 0; row < 4; ++row)
                    {
                        if (!std::isfinite(typedValue[column][row]))
                        {
                            return false;
                        }
                    }
                }
                return true;
            }
            else
            {
                return true;
            }
        },
        value);
}

void validateRenderState(
    const MaterialRenderState& state,
    ValidationReport& report)
{
    switch (state.surfaceType)
    {
    case MaterialSurfaceType::Opaque:
    case MaterialSurfaceType::Transparent:
        break;
    default:
        report.addError(
            "Material.InvalidSurfaceType",
            "renderState.surfaceType",
            "material surface type is invalid");
    }
    switch (state.depth.compare)
    {
    case DepthCompare::Never:
    case DepthCompare::Less:
    case DepthCompare::Equal:
    case DepthCompare::LessEqual:
    case DepthCompare::Greater:
    case DepthCompare::NotEqual:
    case DepthCompare::GreaterEqual:
    case DepthCompare::Always:
        break;
    default:
        report.addError(
            "Material.InvalidDepthCompare",
            "renderState.depth.compare",
            "material depth comparison operation is invalid");
    }
    if (!std::isfinite(state.alphaClipThreshold) ||
        state.alphaClipThreshold < 0.0f ||
        state.alphaClipThreshold > 1.0f)
    {
        report.addError(
            "Material.InvalidAlphaClipThreshold",
            "renderState.alphaClipThreshold",
            "alpha clip threshold must be finite and within [0, 1]");
    }
    if (state.alphaClipEnabled && state.transparent())
    {
        report.addError(
            "Material.InvalidAlphaMode",
            "renderState",
            "alpha clipping currently requires an opaque material");
    }
    if (state.depth.writeEnabled && !state.depth.testEnabled)
    {
        report.addError(
            "Material.InvalidDepthState",
            "renderState.depth",
            "depth writes require depth testing");
    }
}

} // namespace

ValidationReport validateMaterialTemplateCreateInfo(
    const MaterialTemplateAsset::CreateInfo& createInfo,
    const AssetManager& assets)
{
    ValidationReport report;
    static_cast<void>(MaterialTemplateBuilder::build(createInfo, assets, report));
    return report;
}

ValidationReport validateMaterialCreateInfo(
    const MaterialAsset::CreateInfo& createInfo,
    const AssetManager& assets)
{
    ValidationReport report;
    validateRenderState(createInfo.renderState, report);
    if (!assets.contains(createInfo.materialTemplate))
    {
        report.addError(
            "Material.InvalidTemplateHandle",
            "materialTemplate",
            "material template is not owned by this AssetManager");
        return report;
    }

    const MaterialTemplateAsset& materialTemplate =
        assets.materialTemplate(createInfo.materialTemplate);
    if (!assets.isMaterialTemplateCurrent(createInfo.materialTemplate))
    {
        report.addError(
            "Material.StaleTemplateInterface",
            "materialTemplate",
            "material template shader interface signature is stale");
    }

    std::vector<bool> assignedParameters(
        materialTemplate.parameters().size(),
        false);
    for (const MaterialParameterAssignment& assignment :
         createInfo.parameters)
    {
        const auto iterator = std::find_if(
            materialTemplate.parameters().begin(),
            materialTemplate.parameters().end(),
            [&](const MaterialParameterDesc& parameter)
            {
                return parameter.name == assignment.name;
            });
        const std::string path = "parameters." + assignment.name;
        if (iterator == materialTemplate.parameters().end())
        {
            report.addError(
                "Material.UnknownParameter",
                path,
                "parameter is absent from the material template");
            continue;
        }
        const std::size_t index = static_cast<std::size_t>(
            iterator - materialTemplate.parameters().begin());
        if (assignedParameters[index])
        {
            report.addError(
                "Material.DuplicateParameter",
                path,
                "parameter is assigned more than once");
        }
        assignedParameters[index] = true;
        if (materialValueType(assignment.value) != iterator->type)
        {
            report.addError(
                "Material.ParameterTypeMismatch",
                path,
                "assigned value type does not match the template");
        }
        if (!finiteMaterialValue(assignment.value))
        {
            report.addError(
                "Material.NonFiniteParameter",
                path,
                "floating-point material values must be finite");
        }
    }
    for (std::size_t index = 0;
         index < materialTemplate.parameters().size();
         ++index)
    {
        if (materialTemplate.parameters()[index].required &&
            !assignedParameters[index])
        {
            report.addError(
                "Material.RequiredParameterMissing",
                "parameters." + materialTemplate.parameters()[index].name,
                "required material parameter is not assigned");
        }
    }

    uint32_t textureCount = 0;
    for (const MaterialTextureSlotDesc& slot :
         materialTemplate.textureSlots())
    {
        textureCount = std::max(textureCount, slot.slot + 1);
    }
    std::vector<bool> assignedTextures(textureCount, false);
    for (const MaterialTextureAssignment& assignment : createInfo.textures)
    {
        const auto iterator = std::find_if(
            materialTemplate.textureSlots().begin(),
            materialTemplate.textureSlots().end(),
            [&](const MaterialTextureSlotDesc& slot)
            {
                return slot.name == assignment.name;
            });
        const std::string path = "textures." + assignment.name;
        if (iterator == materialTemplate.textureSlots().end())
        {
            report.addError(
                "Material.UnknownTextureSlot",
                path,
                "texture slot is absent from the material template");
            continue;
        }
        if (!assets.contains(assignment.texture))
        {
            report.addError(
                "Material.InvalidTextureHandle",
                path,
                "texture handle is not owned by this AssetManager");
        }
        if (assignedTextures[iterator->slot])
        {
            report.addError(
                "Material.DuplicateTexture",
                path,
                "texture slot is assigned more than once");
        }
        assignedTextures[iterator->slot] = true;
    }
    for (const MaterialTextureSlotDesc& slot :
         materialTemplate.textureSlots())
    {
        if (slot.required && !assignedTextures[slot.slot])
        {
            report.addError(
                "Material.RequiredTextureMissing",
                "textures." + slot.name,
                "required texture slot is not assigned");
        }
    }
    return report;
}

} // namespace rubia::asset
