#pragma once
#include "rubia/ShaderArtifact.hpp"
#include "rhi/PipelineTypes.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace rubia::rhi
{
using DeviceId = uint64_t;
struct DeviceLimits
{
    uint32_t maxBindingGroups = 0;
    uint32_t maxConstantBytes = 0;
};
enum class ResourceErrorCode
{
    InvalidDescription,
    Unsupported,
    DeviceFailure
};
class ResourceError : public std::runtime_error
{
  public:
    ResourceError(ResourceErrorCode code, const std::string &message)
        : std::runtime_error(message), code_(code)
    {
    }
    ResourceErrorCode code() const noexcept { return code_; }

  private:
    ResourceErrorCode code_;
};
enum class Format
{
    Undefined,
    R8UNorm,
    R8Srgb,
    RG8UNorm,
    RG8Srgb,
    RGBA8UNorm,
    RGBA8Srgb,
    BGRA8UNorm,
    BGRA8Srgb,
    RGBA16Float,
    RGBA32Float,
    BC1RGBUNorm,
    BC1RGBSrgb,
    BC1RGBAUNorm,
    BC1RGBASrgb,
    BC2UNorm,
    BC2Srgb,
    BC3UNorm,
    BC3Srgb,
    BC4UNorm,
    BC4SNorm,
    BC5UNorm,
    BC5SNorm,
    BC6HUFloat,
    BC6HSFloat,
    BC7UNorm,
    BC7Srgb,
    D32Float,
    D32FloatS8,
    D24UNormS8
};
enum class BufferUsage : uint32_t
{
    TransferSource = 1,
    TransferDestination = 2,
    Vertex = 4,
    Index = 8,
    Uniform = 16,
    Storage = 32
};
enum class TextureUsage : uint32_t
{
    TransferSource = 1,
    TransferDestination = 2,
    Sampled = 4,
    ColorAttachment = 8,
    DepthStencilAttachment = 16,
    Storage = 32
};
enum class MemoryProperty : uint32_t
{
    DeviceLocal = 1,
    HostVisible = 2,
    HostCoherent = 4,
    HostCached = 8
};
template <class Enum> constexpr uint32_t flag(Enum value) noexcept
{
    return static_cast<uint32_t>(value);
}
struct BufferDesc
{
    uint64_t size = 0;
    uint32_t usage = 0;
    uint32_t requiredMemory = 0;
};
struct Extent3D
{
    uint32_t width = 0, height = 0, depth = 1;
};
struct Offset3D
{
    int32_t x = 0, y = 0, z = 0;
};
enum class TextureDimension
{
    D1,
    D2,
    D3
};
enum class TextureViewDimension
{
    D1,
    D2,
    D3,
    D1Array,
    D2Array,
    Cube,
    CubeArray
};
struct TextureDesc
{
    TextureDimension dimension = TextureDimension::D2;
    Extent3D extent;
    uint32_t mipLevels = 1, arrayLayers = 1, samples = 1;
    Format format = Format::Undefined;
    uint32_t usage = 0;
    uint32_t requiredMemory = flag(MemoryProperty::DeviceLocal);
    bool cubeCompatible = false;
};
enum class TextureAspect : uint32_t
{
    Color = 1,
    Depth = 2,
    Stencil = 4
};
struct SubresourceRange
{
    uint32_t aspects = flag(TextureAspect::Color), baseMipLevel = 0, mipLevels = 1,
             baseArrayLayer = 0, arrayLayers = 1;
};
struct TextureViewDesc
{
    TextureViewDimension dimension = TextureViewDimension::D2;
    Format format = Format::Undefined;
    SubresourceRange range;
};
enum class Filter
{
    Nearest,
    Linear
};
enum class AddressMode
{
    Repeat,
    MirroredRepeat,
    ClampToEdge
};
struct SamplerDesc
{
    Filter minFilter = Filter::Linear, magFilter = Filter::Linear, mipFilter = Filter::Linear;
    AddressMode addressU = AddressMode::Repeat, addressV = AddressMode::Repeat,
                addressW = AddressMode::Repeat;
    float minLod = 0, maxLod = 0, mipLodBias = 0;
};
struct ShaderCode
{
    ShaderStage stage = ShaderStage::Vertex;
    std::string entryPoint = "main";
    ShaderArtifact artifact;
};
enum class AccessUsage
{
    Undefined,
    CopySource,
    CopyDestination,
    VertexRead,
    IndexRead,
    UniformRead,
    ShaderRead,
    ShaderReadWrite,
    ColorWrite,
    DepthReadWrite,
    Present,
    HostRead,
    HostWrite
};
struct ResourceAccess
{
    AccessUsage usage = AccessUsage::Undefined;
    ShaderStageMask stages = 0;
};
struct SourceBytes
{
    std::shared_ptr<const void> owner;
    const std::byte *data = nullptr;
    std::size_t size = 0;
};
struct TextureCopyRegion
{
    uint64_t sourceOffset = 0;
    uint32_t sourceRowLength = 0, sourceImageHeight = 0;
    uint32_t aspects = flag(TextureAspect::Color), mipLevel = 0, baseArrayLayer = 0,
             arrayLayers = 1;
    Offset3D offset;
    Extent3D extent;
};
} // namespace rubia::rhi
