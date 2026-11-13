#pragma once
#include "rhi/Resources.hpp"
#include <array>
namespace rubia::rhi
{
/// Engine clip coordinates are Y-up, with depth in [0, 1]. Backends
/// apply this column-major correction to clip coordinates before rasterization.
/// Identity is appropriate when the backend uses the same convention.
struct ClipSpaceConvention
{
    std::array<float, 16> correction{
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1};
};

/// Resource creation is allowed on the dedicated creation worker. Implementations
/// isolate allocator/descriptor state as required. Queue submission is a separate port.
class Device
{
  public:
    virtual ~Device() = default;
    virtual DeviceId id() const noexcept = 0;
    virtual DeviceLimits limits() const noexcept = 0;
    virtual ClipSpaceConvention clipSpaceConvention() const noexcept { return {}; }
    virtual BufferRef createBuffer(const BufferDesc &) const = 0;
    // Only unpublished allocations or externally synchronized ranges may be written.
    virtual void writeBuffer(const BufferRef &, const void *bytes, uint64_t size,
                             uint64_t offset = 0) const = 0;
    virtual TextureRef createTexture(const TextureDesc &) const = 0;
    virtual TextureViewRef createTextureView(TextureRef, const TextureViewDesc &) const = 0;
    virtual SamplerRef createSampler(const SamplerDesc &) const = 0;
    virtual ShaderRef createShader(const ShaderCode &) const = 0;
    virtual BindingLayoutRef createBindingLayout(const BindingLayoutDesc &) const = 0;
    virtual BindingSetRef createBindingSet(const BindingSetDesc &) const = 0;
};
} // namespace rubia::rhi
