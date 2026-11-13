#include "shader/SpirvReflection.hpp"
#include "shader/ShaderInterfaceSignature.hpp"

#include <spirv_cross.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace rubia::importer::shader
{
namespace
{

[[nodiscard]] spv::ExecutionModel executionModel(asset::ShaderStage stage)
{
    switch (stage)
    {
    case asset::ShaderStage::Vertex: return spv::ExecutionModelVertex;
    case asset::ShaderStage::Fragment: return spv::ExecutionModelFragment;
    case asset::ShaderStage::Compute: return spv::ExecutionModelGLCompute;
    }
    throw std::invalid_argument("unsupported shader stage");
}

[[nodiscard]] asset::ShaderValueType valueType(
    const spirv_cross::SPIRType& type) noexcept
{
    if (!type.array.empty() || (type.width != 32 && type.basetype != spirv_cross::SPIRType::Boolean))
        return asset::ShaderValueType::Unknown;
    using BaseType = spirv_cross::SPIRType::BaseType;
    if (type.columns == 4 && type.vecsize == 4 &&
        type.basetype == BaseType::Float)
    {
        return asset::ShaderValueType::Matrix4;
    }
    if (type.columns != 1)
    {
        return asset::ShaderValueType::Unknown;
    }

    if (type.basetype == BaseType::Float)
    {
        switch (type.vecsize)
        {
        case 1: return asset::ShaderValueType::Float;
        case 2: return asset::ShaderValueType::Float2;
        case 3: return asset::ShaderValueType::Float3;
        case 4: return asset::ShaderValueType::Float4;
        default: return asset::ShaderValueType::Unknown;
        }
    }
    if (type.vecsize != 1)
    {
        return asset::ShaderValueType::Unknown;
    }
    switch (type.basetype)
    {
    case BaseType::Int: return asset::ShaderValueType::Int;
    case BaseType::UInt: return asset::ShaderValueType::UInt;
    case BaseType::Boolean: return asset::ShaderValueType::Bool;
    default: return asset::ShaderValueType::Unknown;
    }
}

[[nodiscard]] uint32_t arrayCount(
    const spirv_cross::SPIRType& type) noexcept
{
    if (type.array.empty())
    {
        return 1;
    }

    uint64_t count = 1;
    for (std::size_t index = 0; index < type.array.size(); ++index)
    {
        if (index >= type.array_size_literal.size() ||
            !type.array_size_literal[index] || type.array[index] == 0)
        {
            return 0;
        }
        count *= type.array[index];
        if (count > std::numeric_limits<uint32_t>::max())
        {
            return 0;
        }
    }
    return static_cast<uint32_t>(count);
}

[[nodiscard]] std::string resourceName(
    const spirv_cross::Compiler& compiler,
    const spirv_cross::Resource& resource)
{
    if (!resource.name.empty())
    {
        return resource.name;
    }
    return compiler.get_name(resource.id);
}

void appendResource(
    asset::ShaderInterface& result,
    const spirv_cross::Compiler& compiler,
    const spirv_cross::Resource& resource,
    asset::ShaderResourceType resourceType)
{
    const spirv_cross::SPIRType& type = compiler.get_type(resource.type_id);
    result.resources.push_back({
        resourceName(compiler, resource),
        compiler.get_decoration(resource.id, spv::DecorationDescriptorSet),
        compiler.get_decoration(resource.id, spv::DecorationBinding),
        resourceType,
        arrayCount(type),
        result.stage
    });
}

// Hash physical layouts recursively, including array/matrix strides. Names and
// SPIR-V IDs are deliberately excluded from compatibility checks.
uint64_t physicalLayoutSignature(
    const spirv_cross::Compiler& compiler, const spirv_cross::SPIRType& type)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    const auto add = [&](uint64_t value)
    {
        for (uint32_t i = 0; i < 8; ++i)
        {
            hash ^= static_cast<uint8_t>(value >> (i * 8));
            hash *= UINT64_C(1099511628211);
        }
    };
    add(type.basetype); add(type.width); add(type.vecsize); add(type.columns);
    add(type.array.size());
    for (std::size_t i = 0; i < type.array.size(); ++i)
    {
        if (i >= type.array_size_literal.size() || !type.array_size_literal[i])
            throw std::invalid_argument("specialized buffer array lengths are unsupported");
        add(type.array[i]);
    }
    if (!type.array.empty())
        add(compiler.get_decoration(type.self, spv::DecorationArrayStride));
    add(type.member_types.size());
    for (uint32_t i = 0; i < type.member_types.size(); ++i)
    {
        add(compiler.type_struct_member_offset(type, i));
        add(compiler.get_member_decoration(type.self, i, spv::DecorationMatrixStride));
        add(compiler.has_member_decoration(type.self, i, spv::DecorationRowMajor));
        const auto& member = compiler.get_type(type.member_types[i]);
        if (!member.array.empty()) add(compiler.type_struct_member_array_stride(type, i));
        add(physicalLayoutSignature(compiler, member));
    }
    return hash;
}

void appendParameterBlock(
    asset::ShaderInterface& result,
    const spirv_cross::Compiler& compiler,
    const spirv_cross::Resource& resource)
{
    const spirv_cross::SPIRType& blockType =
        compiler.get_type(resource.base_type_id);
    asset::ShaderParameterBlockDesc block{};
    block.name = resourceName(compiler, resource);
    block.set =
        compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
    block.binding =
        compiler.get_decoration(resource.id, spv::DecorationBinding);
    block.byteSize = static_cast<uint32_t>(
        compiler.get_declared_struct_size(blockType));
    block.stage = result.stage;
    block.layoutSignature = physicalLayoutSignature(compiler, blockType);
    block.members.reserve(blockType.member_types.size());
    for (uint32_t memberIndex = 0;
         memberIndex < static_cast<uint32_t>(blockType.member_types.size());
         ++memberIndex)
    {
        const spirv_cross::SPIRType& memberType =
            compiler.get_type(blockType.member_types[memberIndex]);
        block.members.push_back({
            compiler.get_member_name(resource.base_type_id, memberIndex),
            valueType(memberType),
            compiler.type_struct_member_offset(blockType, memberIndex),
            static_cast<uint32_t>(
                compiler.get_declared_struct_member_size(
                    blockType,
                    memberIndex)),
            arrayCount(memberType),
            memberType.columns > 1 ? compiler.type_struct_member_matrix_stride(blockType, memberIndex) : 0,
            compiler.has_member_decoration(resource.base_type_id, memberIndex, spv::DecorationRowMajor)
        });
    }
    result.parameterBlocks.push_back(std::move(block));
}

void appendStageIo(
    std::vector<asset::ShaderStageIoDesc>& destination,
    const spirv_cross::Compiler& compiler,
    const spirv_cross::Resource& resource)
{
    if (!compiler.has_decoration(resource.id, spv::DecorationLocation))
    {
        return;
    }
    destination.push_back({
        resourceName(compiler, resource),
        compiler.get_decoration(resource.id, spv::DecorationLocation),
        valueType(compiler.get_type(resource.type_id))
    });
}


} // namespace

asset::ShaderInterface SpirvReflection::reflect(
    const std::vector<uint32_t>& spirv,
    asset::ShaderStage stage,
    const std::string& entryPoint)
{
    if (spirv.empty() || entryPoint.empty())
    {
        throw std::invalid_argument(
            "SPIR-V reflection requires bytecode and an entry point");
    }

    try
    {
        spirv_cross::Compiler compiler(spirv);
        const spv::ExecutionModel model = executionModel(stage);
        bool foundEntryPoint = false;
        for (const spirv_cross::EntryPoint& entry :
             compiler.get_entry_points_and_stages())
        {
            if (entry.name == entryPoint && entry.execution_model == model)
            {
                foundEntryPoint = true;
                break;
            }
        }
        if (!foundEntryPoint)
        {
            throw std::invalid_argument(
                "SPIR-V does not contain the requested stage entry point");
        }
        compiler.set_entry_point(entryPoint, model);

        asset::ShaderInterface result{};
        result.stage = stage;
        result.entryPoint = entryPoint;
        const spirv_cross::ShaderResources resources =
            compiler.get_shader_resources();
        if (!resources.storage_images.empty() || !resources.subpass_inputs.empty() ||
            !resources.atomic_counters.empty() || !resources.acceleration_structures.empty())
            throw std::invalid_argument("shader declares an unsupported resource kind");

        for (const spirv_cross::Resource& resource :
             resources.uniform_buffers)
        {
            appendResource(
                result,
                compiler,
                resource,
                asset::ShaderResourceType::UniformBuffer);
            appendParameterBlock(result, compiler, resource);
        }
        for (const spirv_cross::Resource& resource :
             resources.storage_buffers)
        {
            appendResource(
                result,
                compiler,
                resource,
                asset::ShaderResourceType::StorageBuffer);
            appendParameterBlock(result, compiler, resource);
        }
        for (const spirv_cross::Resource& resource :
             resources.separate_images)
        {
            appendResource(
                result,
                compiler,
                resource,
                asset::ShaderResourceType::SampledImage);
        }
        for (const spirv_cross::Resource& resource :
             resources.separate_samplers)
        {
            appendResource(
                result,
                compiler,
                resource,
                asset::ShaderResourceType::Sampler);
        }
        for (const spirv_cross::Resource& resource :
             resources.sampled_images)
        {
            appendResource(
                result,
                compiler,
                resource,
                asset::ShaderResourceType::CombinedImageSampler);
        }
        for (const spirv_cross::Resource& resource : resources.stage_inputs)
        {
            appendStageIo(result.inputs, compiler, resource);
        }
        for (const spirv_cross::Resource& resource : resources.stage_outputs)
        {
            appendStageIo(result.outputs, compiler, resource);
        }
        for (const spirv_cross::Resource& resource :
             resources.push_constant_buffers)
        {
            const spirv_cross::SPIRType& type =
                compiler.get_type(resource.base_type_id);
            uint32_t offset = 0;
            if (!type.member_types.empty())
            {
                offset = std::numeric_limits<uint32_t>::max();
                for (uint32_t i = 0; i < type.member_types.size(); ++i)
                    offset = std::min(offset, compiler.type_struct_member_offset(type, i));
            }
            result.pushConstants.push_back({
                resourceName(compiler, resource),
                static_cast<uint32_t>(
                    compiler.get_declared_struct_size(type)) - offset,
                stage,
                offset
            });
            auto& push = result.pushConstants.back();
            for (uint32_t i = 0; i < type.member_types.size(); ++i)
            {
                const auto& member = compiler.get_type(type.member_types[i]);
                push.members.push_back({
                    compiler.get_member_name(resource.base_type_id, i), valueType(member),
                    compiler.type_struct_member_offset(type, i),
                    static_cast<uint32_t>(compiler.get_declared_struct_member_size(type, i)),
                    arrayCount(member)});
            }
        }
        for (const auto& constant : compiler.get_specialization_constants())
        {
            const auto& type = compiler.get_type(compiler.get_constant(constant.id).constant_type);
            result.specializationConstants.push_back({constant.constant_id, valueType(type)});
        }

        result.signature = shaderInterfaceSignature(result);
        return result;
    }
    catch (const spirv_cross::CompilerError& error)
    {
        throw std::invalid_argument(
            std::string("SPIR-V reflection failed: ") + error.what());
    }
}

} // namespace rubia::importer::shader
