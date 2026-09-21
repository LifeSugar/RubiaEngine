#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rubia::asset
{

enum class ShaderStage
{
    Vertex,
    Fragment,
    Compute
};

enum class ShaderValueType
{
    Unknown,
    Float,
    Float2,
    Float3,
    Float4,
    Matrix4,
    Int,
    UInt,
    Bool
};

enum class ShaderResourceType
{
    UniformBuffer,
    StorageBuffer,
    SampledImage,
    Sampler,
    CombinedImageSampler
};

struct ShaderResourceDesc
{
    std::string name;
    uint32_t set = 0;
    uint32_t binding = 0;
    ShaderResourceType type = ShaderResourceType::UniformBuffer;
    uint32_t arrayCount = 1;
    ShaderStage stage = ShaderStage::Vertex;
};

struct ShaderBlockMemberDesc
{
    std::string name;
    ShaderValueType type = ShaderValueType::Unknown;
    uint32_t offset = 0;
    uint32_t size = 0;
    uint32_t arrayCount = 1;
    uint32_t matrixStride = 0;
    bool rowMajor = false;
};

struct ShaderParameterBlockDesc
{
    std::string name;
    uint32_t set = 0;
    uint32_t binding = 0;
    uint32_t byteSize = 0;
    ShaderStage stage = ShaderStage::Vertex;
    std::vector<ShaderBlockMemberDesc> members;
    // Recursive physical type/layout signature, including nested structures.
    uint64_t layoutSignature = 0;
};

struct ShaderStageIoDesc
{
    std::string name;
    uint32_t location = 0;
    ShaderValueType type = ShaderValueType::Unknown;
};

struct ShaderSpecializationConstantDesc
{
    uint32_t constantId = 0;
    ShaderValueType type = ShaderValueType::Unknown;
};

struct ShaderPushConstantDesc
{
    std::string name;
    uint32_t byteSize = 0;
    ShaderStage stage = ShaderStage::Vertex;
    uint32_t offset = 0;
    std::vector<ShaderBlockMemberDesc> members;
};

/// Backend-independent interface reflected from one SPIR-V entry point.
struct ShaderInterface
{
    ShaderStage stage = ShaderStage::Vertex;
    std::string entryPoint = "main";
    std::vector<ShaderResourceDesc> resources;
    std::vector<ShaderParameterBlockDesc> parameterBlocks;
    std::vector<ShaderStageIoDesc> inputs;
    std::vector<ShaderStageIoDesc> outputs;
    std::vector<ShaderPushConstantDesc> pushConstants;
    std::vector<ShaderSpecializationConstantDesc> specializationConstants;
    uint64_t signature = 0;
};

} // namespace rubia::asset
