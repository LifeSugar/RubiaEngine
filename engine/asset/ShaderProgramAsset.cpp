#include "asset/ShaderProgramAsset.hpp"

#include "asset/AssetManager.hpp"
#include "asset/ValidationReport.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <tuple>
#include <utility>

namespace rubia::asset
{
namespace
{
// Fixed byte order, no struct padding or process-local handles in signatures.
struct Signature
{
    uint64_t value = UINT64_C(14695981039346656037);
    void add(uint64_t number)
    {
        for (uint32_t i = 0; i < 8; ++i)
        {
            value ^= static_cast<uint8_t>(number >> (i * 8));
            value *= UINT64_C(1099511628211);
        }
    }
    void add(const std::string &string)
    {
        add(string.size());
        for (unsigned char c : string)
        {
            add(c);
        }
    }
};

bool sameBlock(const ShaderParameterBlockDesc &a, const ShaderParameterBlockDesc &b)
{
    if (a.byteSize != b.byteSize || a.layoutSignature != b.layoutSignature ||
        a.members.size() != b.members.size())
    {
        return false;
    }
    for (std::size_t i = 0; i < a.members.size(); ++i)
    {
        const auto &x = a.members[i];
        const auto &y = b.members[i];
        if (x.type != y.type || x.offset != y.offset || x.size != y.size ||
            x.arrayCount != y.arrayCount || x.matrixStride != y.matrixStride ||
            x.rowMajor != y.rowMajor)
        {
            return false;
        }
    }
    return true;
}

void hashIo(Signature &hash, const std::vector<ShaderStageIoDesc> &io)
{
    hash.add(io.size());
    for (const auto &item : io)
    {
        hash.add(item.location);
        hash.add(static_cast<uint64_t>(item.type));
    }
}
} // namespace

ShaderProgramAsset ShaderProgramBuilder::build(ShaderProgramAsset::CreateInfo createInfo,
                                               const AssetManager &assets)
{
    ValidationReport report;
    std::map<ShaderStage, const ShaderAsset *> stages;
    for (auto handle : createInfo.shaders)
    {
        if (!assets.contains(handle))
        {
            report.addError("Program.InvalidShaderHandle", "shaders",
                            "shader handle is invalid or stale");
            continue;
        }
        const auto &shader = assets.shader(handle);
        if (!stages.emplace(shader.stage(), &shader).second)
        {
            report.addError("Program.DuplicateStage", "shaders",
                            "only one shader per stage is allowed");
        }
    }
    if (stages.size() != 2 || !stages.count(ShaderStage::Vertex) ||
        !stages.count(ShaderStage::Fragment))
    {
        report.addError(
            "Program.InvalidStages", "shaders",
            "graphics programs currently require exactly one vertex and one fragment shader");
    }
    if (!report.valid())
    {
        throw AssetValidationError(std::move(report));
    }

    ShaderProgramAsset result;
    result.name_ = std::move(createInfo.name);
    result.shaders_ = std::move(createInfo.shaders);
    std::sort(result.shaders_.begin(), result.shaders_.end(),
              [&](auto a, auto b) { return assets.shader(a).stage() < assets.shader(b).stage(); });

    const auto canonicalIo = [&](std::vector<ShaderStageIoDesc> io) {
        std::sort(io.begin(), io.end(),
                  [](const auto &a, const auto &b) { return a.location < b.location; });
        for (std::size_t i = 0; i < io.size(); ++i)
        {
            if (io[i].type == ShaderValueType::Unknown || io[i].type == ShaderValueType::Matrix4 ||
                (i && io[i - 1].location == io[i].location))
            {
                report.addError(
                    "Program.UnsupportedStageIo", "location " + std::to_string(io[i].location),
                    "stage interface requires unique locations and supported scalar/vector types");
            }
        }
        return io;
    };
    result.interface_.vertexInputs =
        canonicalIo(stages.at(ShaderStage::Vertex)->interface().inputs);
    result.interface_.fragmentOutputs =
        canonicalIo(stages.at(ShaderStage::Fragment)->interface().outputs);
    const auto outputs = canonicalIo(stages.at(ShaderStage::Vertex)->interface().outputs);
    const auto inputs = canonicalIo(stages.at(ShaderStage::Fragment)->interface().inputs);
    for (const auto &input : inputs)
    {
        const auto output = std::find_if(outputs.begin(), outputs.end(), [&](const auto &x) {
            return x.location == input.location;
        });
        if (output == outputs.end() || output->type != input.type)
        {
            report.addError(
                "Program.StageIoMismatch",
                "fragment.inputs[" + std::to_string(input.location) + "]",
                "fragment input requires a vertex output with the same location and type");
        }
    }

    std::map<std::pair<uint32_t, uint32_t>, ProgramResourceBinding> bindings;
    Signature code;
    for (const auto &[stage, shader] : stages)
    {
        const auto mask = shaderStageMask(stage);
        code.add(static_cast<uint64_t>(stage));
        code.add(shader->entryPoint());
        code.add(shader->spirv().size());
        for (auto word : shader->spirv())
        {
            code.add(word);
        }
        std::set<std::pair<uint32_t, uint32_t>> stageBindings;
        for (const auto &resource : shader->interface().resources)
        {
            const auto key = std::make_pair(resource.set, resource.binding);
            const std::string path = "set " + std::to_string(resource.set) + " binding " +
                                     std::to_string(resource.binding);
            if (!stageBindings.insert(key).second)
            {
                report.addError("Program.DuplicateBinding", path,
                                "stage declares a binding more than once");
            }
            ProgramResourceBinding binding;
            binding.set = resource.set;
            binding.binding = resource.binding;
            binding.type = resource.type;
            binding.arrayCount = resource.arrayCount;
            binding.stages = mask;
            if (resource.arrayCount == 0)
            {
                report.addError("Program.UnsupportedDescriptorArray", path,
                                "runtime or specialized descriptor arrays are not supported");
            }
            if (!resource.name.empty())
            {
                binding.names.push_back(resource.name);
            }
            if (resource.type == ShaderResourceType::UniformBuffer ||
                resource.type == ShaderResourceType::StorageBuffer)
            {
                const auto &blocks = shader->interface().parameterBlocks;
                const auto block = std::find_if(blocks.begin(), blocks.end(), [&](const auto &x) {
                    return x.set == resource.set && x.binding == resource.binding;
                });
                if (block == blocks.end())
                {
                    report.addError("Program.BufferLayoutMissing", path,
                                    "buffer requires reflected layout information");
                }
                else
                {
                    binding.bufferLayout = *block;
                }
            }
            auto [existing, inserted] = bindings.emplace(key, binding);
            if (!inserted)
            {
                auto &merged = existing->second;
                if (merged.type != binding.type || merged.arrayCount != binding.arrayCount ||
                    merged.bufferLayout.has_value() != binding.bufferLayout.has_value() ||
                    (merged.bufferLayout && binding.bufferLayout &&
                     !sameBlock(*merged.bufferLayout, *binding.bufferLayout)))
                {
                    report.addError(
                        "Program.ResourceMismatch", path,
                        "shader stages declare incompatible resource or buffer layouts");
                }
                merged.stages |= mask;
                merged.names.insert(merged.names.end(), binding.names.begin(), binding.names.end());
                // Material members are addressed by name; ambiguous cross-stage
                // names cannot form a single material schema.
                if (merged.bufferLayout && binding.bufferLayout &&
                    sameBlock(*merged.bufferLayout, *binding.bufferLayout))
                {
                    for (std::size_t i = 0; i < merged.bufferLayout->members.size(); ++i)
                    {
                        if (merged.bufferLayout->members[i].name !=
                            binding.bufferLayout->members[i].name)
                        {
                            report.addError("Program.BufferMemberNameMismatch", path,
                                            "shared buffer members require consistent names");
                        }
                    }
                }
            }
        }
        if (shader->interface().pushConstants.size() > 1)
        {
            report.addError("Program.MultiplePushConstantBlocks", "pushConstants",
                            "only one push constant block per stage is supported");
        }
        for (const auto &push : shader->interface().pushConstants)
        {
            if (push.byteSize == 0 || push.offset % 4 || push.byteSize % 4 ||
                push.offset > std::numeric_limits<uint32_t>::max() - push.byteSize)
            {
                report.addError("Program.InvalidPushConstantRange", "pushConstants",
                                "push constant range must be non-empty, aligned, and bounded");
            }
            auto &ranges = result.interface_.pushConstants;
            const auto existing = std::find_if(ranges.begin(), ranges.end(), [&](const auto &x) {
                return x.offset == push.offset && x.size == push.byteSize;
            });
            if (existing == ranges.end())
            {
                ranges.push_back({push.offset, push.byteSize, mask});
            }
            else
            {
                existing->stages |= mask;
            }
        }
    }
    if (!report.valid())
    {
        throw AssetValidationError(std::move(report));
    }
    for (auto &[key, binding] : bindings)
    {
        std::sort(binding.names.begin(), binding.names.end());
        binding.names.erase(std::unique(binding.names.begin(), binding.names.end()),
                            binding.names.end());
        result.interface_.bindings.push_back(std::move(binding));
    }
    auto &ranges = result.interface_.pushConstants;
    std::sort(ranges.begin(), ranges.end(), [](const auto &a, const auto &b) {
        return std::tie(a.offset, a.size, a.stages) < std::tie(b.offset, b.size, b.stages);
    });

    Signature layout;
    layout.add(result.interface_.bindings.size());
    for (const auto &binding : result.interface_.bindings)
    {
        layout.add(binding.set);
        layout.add(binding.binding);
        layout.add(static_cast<uint64_t>(binding.type));
        layout.add(binding.arrayCount);
        layout.add(binding.stages);
    }
    layout.add(ranges.size());
    for (const auto &push : ranges)
    {
        layout.add(push.offset);
        layout.add(push.size);
        layout.add(push.stages);
    }
    Signature interface = layout;
    for (const auto &binding : result.interface_.bindings)
    {
        interface.add(binding.names.size());
        for (const auto &name : binding.names)
        {
            interface.add(name);
        }
        if (binding.bufferLayout)
        {
            const auto &block = *binding.bufferLayout;
            interface.add(block.byteSize);
            interface.add(block.layoutSignature);
            interface.add(block.members.size());
            for (const auto &member : block.members)
            {
                interface.add(member.name);
                interface.add(static_cast<uint64_t>(member.type));
                interface.add(member.offset);
                interface.add(member.size);
                interface.add(member.arrayCount);
                interface.add(member.matrixStride);
                interface.add(member.rowMajor);
            }
        }
    }
    hashIo(interface, result.interface_.vertexInputs);
    hashIo(interface, outputs);
    hashIo(interface, inputs);
    hashIo(interface, result.interface_.fragmentOutputs);
    // Specialization and push-member types are shader-interface metadata, not
    // descriptor/pipeline-layout identity. Preserve stage-local constant IDs.
    for (const auto& [stage, shader] : stages)
    {
        interface.add(static_cast<uint64_t>(stage));
        auto constants = shader->interface().specializationConstants;
        std::sort(constants.begin(), constants.end(), [](const auto& a, const auto& b)
            { return a.constantId < b.constantId; });
        interface.add(constants.size());
        for (const auto& constant : constants)
        {
            interface.add(constant.constantId);
            interface.add(static_cast<uint64_t>(constant.type));
        }
        for (const auto& push : shader->interface().pushConstants)
        {
            interface.add(push.members.size());
            for (const auto& member : push.members)
            {
                interface.add(member.offset);
                interface.add(member.size);
                interface.add(static_cast<uint64_t>(member.type));
                interface.add(member.arrayCount);
            }
        }
    }
    result.codeSignature_ = code.value;
    result.layoutSignature_ = layout.value;
    result.interfaceSignature_ = interface.value;
    return result;
}
} // namespace rubia::asset
