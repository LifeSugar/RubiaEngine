#pragma once

#include <cstdint>
#include <cstddef>
#include <variant>
#include <vector>

namespace rubia::rhi
{

enum class ShaderStage : uint32_t
{
    Vertex = 1u << 0,
    Fragment = 1u << 1,
    Compute = 1u << 2
};
using ShaderStageMask = uint32_t;
[[nodiscard]] constexpr ShaderStageMask shaderStageMask(ShaderStage stage) noexcept
{
    return static_cast<ShaderStageMask>(stage);
}

enum class BindingType
{
    Unknown,
    UniformBuffer,
    StorageBuffer,
    SampledTexture,
    Sampler,
    CombinedTextureSampler
};

struct BindingDesc
{
    uint32_t binding = 0;
    BindingType type = BindingType::Unknown;
    uint32_t count = 1;
    ShaderStageMask stages = 0;
};

/// Group numbering is supplied by the pipeline's ordered layout list.
struct BindingLayoutDesc
{
    std::vector<BindingDesc> bindings;
};

enum class VertexFormat
{
    Float,
    Float2,
    Float3,
    Float4
};
enum class VertexInputRate
{
    Vertex,
    Instance
};

struct VertexBindingDesc
{
    uint32_t binding = 0;
    uint32_t stride = 0;
    VertexInputRate rate = VertexInputRate::Vertex;
};
struct VertexAttributeDesc
{
    uint32_t location = 0;
    uint32_t binding = 0;
    VertexFormat format = VertexFormat::Float;
    uint32_t offset = 0;
};
struct VertexLayoutDesc
{
    std::vector<VertexBindingDesc> bindings;
    std::vector<VertexAttributeDesc> attributes;
};

struct ConstantRangeDesc
{
    ShaderStageMask stages = 0;
    uint32_t offset = 0;
    uint32_t size = 0;
};

/// Typed values deliberately leave backend byte packing out of the contract.
struct SpecializationConstant
{
    ShaderStage stage = ShaderStage::Fragment;
    uint32_t id = 0;
    std::variant<bool, int32_t, uint32_t, float, std::vector<std::byte>> value = false;
};

enum class CompareOp
{
    Never,
    Less,
    Equal,
    LessEqual,
    Greater,
    NotEqual,
    GreaterEqual,
    Always
};
enum class CullMode
{
    None,
    Front,
    Back,
    FrontAndBack
};
enum class BlendFactor
{
    Zero,
    One,
    SourceAlpha,
    OneMinusSourceAlpha,
    SourceColor,
    OneMinusSourceColor,
    DestinationColor,
    OneMinusDestinationColor,
    DestinationAlpha,
    OneMinusDestinationAlpha,
    ConstantColor,
    OneMinusConstantColor,
    ConstantAlpha,
    OneMinusConstantAlpha,
    SourceAlphaSaturate,
    Source1Color,
    OneMinusSource1Color,
    Source1Alpha,
    OneMinusSource1Alpha
};
enum class BlendOp
{
    Add,
    Subtract,
    ReverseSubtract,
    Minimum,
    Maximum
};

/// Blend arithmetic only; the pass retains its attachment write mask.
struct ColorBlendDesc
{
    bool enabled = false;
    BlendFactor sourceColor = BlendFactor::One;
    BlendFactor destinationColor = BlendFactor::Zero;
    BlendOp colorOperation = BlendOp::Add;
    BlendFactor sourceAlpha = BlendFactor::One;
    BlendFactor destinationAlpha = BlendFactor::Zero;
    BlendOp alphaOperation = BlendOp::Add;
};

struct Viewport
{
    float x = 0, y = 0, width = 1, height = 1, minDepth = 0, maxDepth = 1;
};
struct Rect
{
    int32_t x = 0, y = 0;
    uint32_t width = 1, height = 1;
};

} // namespace rubia::rhi
