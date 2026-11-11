#pragma once
#include "rhi/ResourceTypes.hpp"
#include <variant>

namespace rubia::rhi
{
/// References retain CPU owners only. Submitted uses must complete before last release.
class Resource
{
  public:
    virtual ~Resource() = default;
    virtual DeviceId deviceId() const noexcept = 0;
};
class BufferResource : public Resource
{
  public:
    virtual const BufferDesc &description() const noexcept = 0;
    virtual uint32_t memoryProperties() const noexcept = 0;
};
class TextureResource : public Resource
{
  public:
    virtual const TextureDesc &description() const noexcept = 0;
};
using BufferRef = std::shared_ptr<const BufferResource>;
using TextureRef = std::shared_ptr<const TextureResource>;
class TextureViewResource : public Resource
{
  public:
    virtual const TextureViewDesc &description() const noexcept = 0;
    virtual const TextureRef &texture() const noexcept = 0;
};
class SamplerResource : public Resource
{
  public:
    virtual const SamplerDesc &description() const noexcept = 0;
};
class ShaderResource : public Resource
{
  public:
    virtual const ShaderCode &code() const noexcept = 0;
};
class BindingLayoutResource : public Resource
{
  public:
    virtual const BindingLayoutDesc &description() const noexcept = 0;
};
using TextureViewRef = std::shared_ptr<const TextureViewResource>;
using SamplerRef = std::shared_ptr<const SamplerResource>;
using ShaderRef = std::shared_ptr<const ShaderResource>;
using BindingLayoutRef = std::shared_ptr<const BindingLayoutResource>;
struct BufferBinding
{
    BufferRef buffer;
    uint64_t offset = 0, size = 0;
};
struct TextureBinding
{
    TextureViewRef view;
    ResourceAccess access{AccessUsage::ShaderRead, 0};
};
struct CombinedTextureBinding
{
    TextureViewRef view;
    SamplerRef sampler;
    ResourceAccess access{AccessUsage::ShaderRead, 0};
};
struct BindingWrite
{
    uint32_t binding = 0, arrayElement = 0;
    std::variant<BufferBinding, TextureBinding, SamplerRef, CombinedTextureBinding> resource;
};
struct BindingSetDesc
{
    BindingLayoutRef layout;
    std::vector<BindingWrite> writes;
};
/// Immutable allocation. Owns its layout and all referenced buffers/views/samplers.
class BindingSetResource : public Resource
{
  public:
    virtual const BindingSetDesc &description() const noexcept = 0;
};
using BindingSetRef = std::shared_ptr<const BindingSetResource>;
} // namespace rubia::rhi
