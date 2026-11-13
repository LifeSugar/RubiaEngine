#include "asset/MaterialTemplateBuilder.hpp"

#include "asset/AssetManager.hpp"

#include <algorithm>
#include <set>
#include <unordered_set>

namespace rubia::asset
{
namespace
{
std::optional<MaterialValueType> materialType(ShaderValueType type)
{
    switch (type)
    {
    case ShaderValueType::Float:
        return MaterialValueType::Float;
    case ShaderValueType::Float2:
        return MaterialValueType::Float2;
    case ShaderValueType::Float3:
        return MaterialValueType::Float3;
    case ShaderValueType::Float4:
        return MaterialValueType::Float4;
    case ShaderValueType::Matrix4:
        return MaterialValueType::Matrix4;
    case ShaderValueType::Int:
        return MaterialValueType::Int;
    case ShaderValueType::UInt:
        return MaterialValueType::UInt;
    case ShaderValueType::Bool:
        return MaterialValueType::Bool;
    default:
        return std::nullopt;
    }
}
} // namespace

MaterialTemplateAsset::CompiledCreateInfo MaterialTemplateBuilder::build(
    const MaterialTemplateAsset::CreateInfo &createInfo, const AssetManager &assets,
    ValidationReport &report)
{
    MaterialTemplateAsset::CompiledCreateInfo result;
    result.name = createInfo.name;
    result.program = createInfo.program;
    if (!assets.contains(createInfo.program))
    {
        report.addError("Template.InvalidProgram", "program", "program handle is invalid or stale");
        return result;
    }
    const auto &program = assets.shaderProgram(createInfo.program);
    result.programInterfaceSignature = program.interfaceSignature();
    for (const auto &binding : program.interface().bindings)
    {
        if (binding.set == createInfo.materialSet)
        {
            result.bindings.push_back(binding);
        }
    }

    const ProgramResourceBinding *parameterBinding = nullptr;
    for (const auto &binding : result.bindings)
    {
        const auto path = "binding " + std::to_string(binding.binding);
        if (binding.arrayCount != 1)
        {
            report.addError("Template.UnsupportedResource", path,
                            "material descriptor arrays are not supported");
        }
        if (binding.type == ShaderResourceType::UniformBuffer)
        {
            if (parameterBinding)
            {
                report.addError("Template.MultipleParameterBlocks", path,
                                "material currently supports one uniform buffer");
            }
            parameterBinding = &binding;
        }
        else if (binding.type != ShaderResourceType::SampledImage &&
                 binding.type != ShaderResourceType::Sampler)
        {
            report.addError("Template.UnsupportedResource", path,
                            "material requires a uniform buffer and separate images/samplers");
        }
    }
    if (!parameterBinding || !parameterBinding->bufferLayout)
    {
        report.addError("Template.ParameterBlockMissing", "materialSet",
                        "material set requires a reflected uniform buffer");
        return result;
    }
    result.parameterBlock.descriptor = {createInfo.materialSet, parameterBinding->binding};
    const auto &block = *parameterBinding->bufferLayout;
    result.parameterDataSize = block.byteSize;
    std::unordered_set<std::string> names;
    for (const auto &member : block.members)
    {
        const auto type = materialType(member.type);
        const auto path = "parameters." + member.name;
        if (!type || member.arrayCount != 1 ||
            (*type == MaterialValueType::Matrix4 && (member.rowMajor || member.matrixStride != 16)))
        {
            report.addError("Template.UnsupportedParameter", path,
                            "material requires scalar/vector or column-major float4x4 members; "
                            "arrays and nested structs are unsupported");
            continue;
        }
        if (member.name.empty() || !names.insert(member.name).second)
        {
            report.addError("Template.InvalidParameterName", path,
                            "reflected parameter names must be non-empty and unique");
        }
        const auto size = MaterialTemplateAsset::valueSize(*type);
        if (member.size != size || member.offset > block.byteSize ||
            size > block.byteSize - member.offset)
        {
            report.addError("Template.InvalidParameterLayout", path,
                            "reflected member size or range is unsupported");
        }
        for (const auto &previous : result.parameters)
        {
            // uint64_t avoids overflow even for invalid reflected input.
            if (std::max<uint64_t>(previous.byteOffset, member.offset) <
                std::min<uint64_t>(uint64_t(previous.byteOffset) +
                                       MaterialTemplateAsset::valueSize(previous.type),
                                   uint64_t(member.offset) + size))
            {
                report.addError("Template.InvalidParameterLayout", path,
                                "reflected parameter ranges overlap");
            }
        }
        result.parameters.push_back({member.name, *type, member.offset, true});
    }
    std::unordered_set<std::string> metadataNames;
    for (const auto &metadata : createInfo.parameters)
    {
        const auto parameter = std::find_if(result.parameters.begin(), result.parameters.end(),
                                            [&](const auto &p) { return p.name == metadata.name; });
        if (parameter == result.parameters.end() || !metadataNames.insert(metadata.name).second)
        {
            report.addError("Template.InvalidParameterMetadata", "parameters." + metadata.name,
                            "metadata must refer to a unique reflected parameter");
        }
        else
        {
            parameter->required = metadata.required;
        }
    }

    const auto findResource = [&](const std::string &name,
                                  ShaderResourceType type) -> const ProgramResourceBinding * {
        const ProgramResourceBinding *found = nullptr;
        for (const auto &binding : result.bindings)
        {
            if (binding.type == type &&
                std::find(binding.names.begin(), binding.names.end(), name) != binding.names.end())
            {
                if (found)
                {
                    return nullptr;
                }
                found = &binding;
            }
        }
        return found;
    };
    std::set<uint32_t> usedImages;
    std::set<uint32_t> usedSamplers;
    for (const auto &metadata : createInfo.textureSlots)
    {
        const auto *image = findResource(metadata.imageResource, ShaderResourceType::SampledImage);
        const auto *sampler = findResource(metadata.samplerResource, ShaderResourceType::Sampler);
        if (metadata.name.empty() || !names.insert(metadata.name).second)
        {
            report.addError("Template.InvalidTextureName", "textures." + metadata.name,
                            "texture slot names must be non-empty and unique");
        }
        if (!image || !sampler)
        {
            report.addError(
                "Template.TextureResourceMissing", "textures." + metadata.name,
                "image and sampler names must resolve unambiguously in the material set");
            continue;
        }
        if (!usedImages.insert(image->binding).second ||
            !usedSamplers.insert(sampler->binding).second)
        {
            report.addError("Template.DuplicateTextureResource", "textures." + metadata.name,
                            "each image and sampler currently belongs to one slot");
        }
        result.textureSlots.push_back({metadata.name,
                                       static_cast<uint32_t>(result.textureSlots.size()),
                                       metadata.required,
                                       {image->set, image->binding},
                                       {sampler->set, sampler->binding}});
    }
    for (const auto &binding : result.bindings)
    {
        if ((binding.type == ShaderResourceType::SampledImage &&
             !usedImages.count(binding.binding)) ||
            (binding.type == ShaderResourceType::Sampler && !usedSamplers.count(binding.binding)))
        {
            report.addError("Template.UnmappedTextureResource",
                            "binding " + std::to_string(binding.binding),
                            "material image/sampler requires semantic slot metadata");
        }
    }

    uint64_t hash = UINT64_C(14695981039346656037);
    const auto add = [&](uint64_t value) {
        for (uint32_t i = 0; i < 8; ++i)
        {
            hash ^= static_cast<uint8_t>(value >> (i * 8));
            hash *= UINT64_C(1099511628211);
        }
    };
    const auto addName = [&](const std::string &name) {
        add(name.size());
        for (unsigned char c : name)
        {
            add(c);
        }
    };
    add(createInfo.materialSet);
    add(parameterBinding->binding);
    add(result.parameterDataSize);
    add(result.parameters.size());
    for (const auto &parameter : result.parameters)
    {
        addName(parameter.name);
        add(static_cast<uint64_t>(parameter.type));
        add(parameter.byteOffset);
        add(parameter.required);
    }
    add(result.textureSlots.size());
    for (const auto &slot : result.textureSlots)
    {
        addName(slot.name);
        add(slot.slot);
        add(slot.required);
        add(slot.imageBinding.binding);
        add(slot.samplerBinding.binding);
    }
    result.schemaSignature = hash;
    return result;
}
} // namespace rubia::asset
