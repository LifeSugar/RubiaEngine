#include "SpirvCode.hpp"
#include "VulkanResourceDevice.hpp"
#include "vulkan/ImageView.hpp"
#include "vulkan/Sampler.hpp"
#include "vulkan/DescriptorPool.hpp"
#include <algorithm>
#include <atomic>
#include <limits>
#include <map>
#include <set>
#include <type_traits>

namespace rubia::rhi::vulkan
{
namespace
{
[[noreturn]] void invalid(const char *message)
{
    throw ResourceError(ResourceErrorCode::InvalidDescription, message);
}
uint32_t memoryFlags(VkMemoryPropertyFlags bits)
{
    uint32_t result = 0;
    if (bits & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
        result |= flag(MemoryProperty::DeviceLocal);
    if (bits & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
        result |= flag(MemoryProperty::HostVisible);
    if (bits & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
        result |= flag(MemoryProperty::HostCoherent);
    if (bits & VK_MEMORY_PROPERTY_HOST_CACHED_BIT)
        result |= flag(MemoryProperty::HostCached);
    return result;
}
VkMemoryPropertyFlags nativeMemory(uint32_t bits)
{
    if (bits & ~15u)
        invalid("unknown required memory properties");
    VkMemoryPropertyFlags result = 0;
    if (bits & flag(MemoryProperty::DeviceLocal))
        result |= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    if (bits & flag(MemoryProperty::HostVisible))
        result |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    if (bits & flag(MemoryProperty::HostCoherent))
        result |= VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    if (bits & flag(MemoryProperty::HostCached))
        result |= VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    return result;
}
VkDescriptorType nativeBindingType(BindingType type)
{
    switch (type)
    {
    case BindingType::UniformBuffer:
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    case BindingType::StorageBuffer:
        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    case BindingType::SampledTexture:
        return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    case BindingType::Sampler:
        return VK_DESCRIPTOR_TYPE_SAMPLER;
    case BindingType::CombinedTextureSampler:
        return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    default:
        invalid("unknown binding type");
    }
}
struct BufferOwner final : BufferResource
{
    DeviceId device;
    BufferDesc desc;
    mutable vulkan::Buffer value;
    DeviceId deviceId() const noexcept override { return device; }
    const BufferDesc &description() const noexcept override { return desc; }
    uint32_t memoryProperties() const noexcept override
    {
        return memoryFlags(value.memoryProperties());
    }
};
struct TextureOwner final : TextureResource
{
    DeviceId device;
    TextureDesc desc;
    vulkan::Image value;
    DeviceId deviceId() const noexcept override { return device; }
    const TextureDesc &description() const noexcept override { return desc; }
};
struct ViewOwner final : TextureViewResource
{
    DeviceId device;
    TextureViewDesc desc;
    TextureRef source;
    vulkan::ImageView value;
    DeviceId deviceId() const noexcept override { return device; }
    const TextureViewDesc &description() const noexcept override { return desc; }
    const TextureRef &texture() const noexcept override { return source; }
};
struct SamplerOwner final : SamplerResource
{
    DeviceId device;
    SamplerDesc desc;
    vulkan::Sampler value;
    DeviceId deviceId() const noexcept override { return device; }
    const SamplerDesc &description() const noexcept override { return desc; }
};
struct ShaderOwner final : ShaderResource
{
    DeviceId device;
    ShaderCode desc;
    VkDevice nativeDevice = VK_NULL_HANDLE;
    VkShaderModule value = VK_NULL_HANDLE;
    ~ShaderOwner() override
    {
        if (value)
            vkDestroyShaderModule(nativeDevice, value, nullptr);
    }
    DeviceId deviceId() const noexcept override { return device; }
    const ShaderCode &code() const noexcept override { return desc; }
};
struct LayoutOwner final : BindingLayoutResource
{
    DeviceId device;
    BindingLayoutDesc desc;
    vulkan::DescriptorSetLayout value;
    std::shared_ptr<const DescriptorSetLayoutState> borrowed;
    DeviceId deviceId() const noexcept override { return device; }
    const BindingLayoutDesc &description() const noexcept override { return desc; }
};
struct SetOwner final : BindingSetResource
{
    DeviceId device;
    BindingSetDesc desc;
    vulkan::DescriptorPool pool;
    VkDescriptorSet value = VK_NULL_HANDLE;
    DeviceId deviceId() const noexcept override { return device; }
    const BindingSetDesc &description() const noexcept override { return desc; }
};
template <class Owner, class Ref> const Owner &checked(const Ref &ref)
{
    const auto *result = dynamic_cast<const Owner *>(ref.get());
    if (!result)
        invalid("resource does not belong to the Vulkan resource adapter");
    return *result;
}
void sameDevice(DeviceId id, const Resource &resource)
{
    if (resource.deviceId() != id)
        invalid("resources belong to different device sessions");
}
VkSamplerAddressMode address(AddressMode value)
{
    switch (value)
    {
    case AddressMode::Repeat:
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case AddressMode::MirroredRepeat:
        return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case AddressMode::ClampToEdge:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    }
    invalid("invalid sampler address mode");
}
} // namespace
VulkanResourceDevice::VulkanResourceDevice(const vulkan::Device &device) : device_(device)
{
    id_ = device.lifetimeId();
    if (!device_ || !id_)
        invalid("invalid resource device session");
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(device_.physical(), &properties);
    limits_ = {properties.limits.maxBoundDescriptorSets, properties.limits.maxPushConstantsSize};
}
const vulkan::Buffer &nativeBuffer(const BufferRef &value)
{
    return checked<BufferOwner>(value).value;
}
const vulkan::Image &nativeImage(const TextureRef &value)
{
    return checked<TextureOwner>(value).value;
}
VkImageView nativeTextureView(const TextureViewRef &value)
{
    return checked<ViewOwner>(value).value.get();
}
VkSampler nativeSampler(const SamplerRef &value)
{
    return checked<SamplerOwner>(value).value.get();
}
VkShaderModule nativeShader(const ShaderRef &value) { return checked<ShaderOwner>(value).value; }
VkDevice nativeShaderDevice(const ShaderRef &value)
{
    return checked<ShaderOwner>(value).nativeDevice;
}
std::shared_ptr<const DescriptorSetLayoutState> nativeBindingLayout(const BindingLayoutRef &value)
{
    const auto &owner = checked<LayoutOwner>(value);
    return owner.borrowed ? owner.borrowed : owner.value.reference();
}
BindingLayoutRef borrowBindingLayout(const VulkanResourceDevice &device,
                                     std::shared_ptr<const DescriptorSetLayoutState> state)
{
    if (!state || state->device() != device.native().get())
        invalid("borrowed layout belongs to another device");
    auto result = std::make_shared<LayoutOwner>();
    result->device = device.id();
    result->borrowed = std::move(state);
    for (const auto &input : result->borrowed->bindings())
    {
        BindingType type;
        switch (input.descriptorType)
        {
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
            type = BindingType::UniformBuffer;
            break;
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
            type = BindingType::StorageBuffer;
            break;
        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
            type = BindingType::SampledTexture;
            break;
        case VK_DESCRIPTOR_TYPE_SAMPLER:
            type = BindingType::Sampler;
            break;
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
            type = BindingType::CombinedTextureSampler;
            break;
        default:
            throw ResourceError(ResourceErrorCode::Unsupported,
                                "borrowed descriptor type outside RHI subset");
        }
        if (input.pImmutableSamplers)
            throw ResourceError(ResourceErrorCode::Unsupported,
                                "immutable sampler layouts outside RHI subset");
        ShaderStageMask stages = 0;
        if (input.stageFlags & VK_SHADER_STAGE_VERTEX_BIT)
            stages |= shaderStageMask(ShaderStage::Vertex);
        if (input.stageFlags & VK_SHADER_STAGE_FRAGMENT_BIT)
            stages |= shaderStageMask(ShaderStage::Fragment);
        if (input.stageFlags & VK_SHADER_STAGE_COMPUTE_BIT)
            stages |= shaderStageMask(ShaderStage::Compute);
        if (toVulkanStages(stages) != input.stageFlags)
            throw ResourceError(ResourceErrorCode::Unsupported,
                                "borrowed shader stage outside RHI subset");
        result->desc.bindings.push_back({input.binding, type, input.descriptorCount, stages});
    }
    return result;
}
VkDescriptorSet nativeBindingSet(const BindingSetRef &value)
{
    return checked<SetOwner>(value).value;
}
VkShaderStageFlags toVulkanStages(ShaderStageMask mask)
{
    if (mask & ~7u)
        invalid("unsupported shader stage");
    VkShaderStageFlags result = 0;
    if (mask & shaderStageMask(ShaderStage::Vertex))
        result |= VK_SHADER_STAGE_VERTEX_BIT;
    if (mask & shaderStageMask(ShaderStage::Fragment))
        result |= VK_SHADER_STAGE_FRAGMENT_BIT;
    if (mask & shaderStageMask(ShaderStage::Compute))
        result |= VK_SHADER_STAGE_COMPUTE_BIT;
    return result;
}
VkImageAspectFlags toVulkanAspects(uint32_t mask)
{
    if (!mask || (mask & ~7u))
        invalid("invalid texture aspect mask");
    VkImageAspectFlags result = 0;
    if (mask & flag(TextureAspect::Color))
        result |= VK_IMAGE_ASPECT_COLOR_BIT;
    if (mask & flag(TextureAspect::Depth))
        result |= VK_IMAGE_ASPECT_DEPTH_BIT;
    if (mask & flag(TextureAspect::Stencil))
        result |= VK_IMAGE_ASPECT_STENCIL_BIT;
    return result;
}
#define RUBIA_FORMATS(X)                                                                           \
    X(Undefined, UNDEFINED)                                                                        \
    X(R8UNorm, R8_UNORM) X(R8Srgb, R8_SRGB) X(RG8UNorm, R8G8_UNORM) X(RG8Srgb, R8G8_SRGB)          \
        X(RGBA8UNorm, R8G8B8A8_UNORM) X(RGBA8Srgb, R8G8B8A8_SRGB) X(BGRA8UNorm, B8G8R8A8_UNORM)    \
            X(BGRA8Srgb, B8G8R8A8_SRGB) X(RGBA16Float, R16G16B16A16_SFLOAT)                        \
                X(RGBA32Float, R32G32B32A32_SFLOAT) X(BC1RGBUNorm, BC1_RGB_UNORM_BLOCK)            \
                    X(BC1RGBSrgb, BC1_RGB_SRGB_BLOCK) X(BC1RGBAUNorm, BC1_RGBA_UNORM_BLOCK)        \
                        X(BC1RGBASrgb, BC1_RGBA_SRGB_BLOCK) X(BC2UNorm, BC2_UNORM_BLOCK)           \
                            X(BC2Srgb, BC2_SRGB_BLOCK) X(BC3UNorm, BC3_UNORM_BLOCK)                \
                                X(BC3Srgb, BC3_SRGB_BLOCK) X(BC4UNorm, BC4_UNORM_BLOCK) X(         \
                                    BC4SNorm, BC4_SNORM_BLOCK) X(BC5UNorm, BC5_UNORM_BLOCK)        \
                                    X(BC5SNorm, BC5_SNORM_BLOCK) X(BC6HUFloat, BC6H_UFLOAT_BLOCK)  \
                                        X(BC6HSFloat, BC6H_SFLOAT_BLOCK)                           \
                                            X(BC7UNorm, BC7_UNORM_BLOCK)                           \
                                                X(BC7Srgb, BC7_SRGB_BLOCK) X(D32Float, D32_SFLOAT) \
                                                    X(D32FloatS8, D32_SFLOAT_S8_UINT)              \
                                                        X(D24UNormS8, D24_UNORM_S8_UINT)
VkFormat toVulkan(Format value)
{
    switch (value)
    {
#define X(a, b)                                                                                    \
    case Format::a:                                                                                \
        return VK_FORMAT_##b;
        RUBIA_FORMATS(X)
#undef X
    }
    invalid("unknown texture format");
}
Format fromVulkanFormat(VkFormat value)
{
    switch (value)
    {
#define X(a, b)                                                                                    \
    case VK_FORMAT_##b:                                                                            \
        return Format::a;
        RUBIA_FORMATS(X)
#undef X
    default:
        throw ResourceError(ResourceErrorCode::Unsupported,
                            "Vulkan format is outside the RHI subset");
    }
}
#undef RUBIA_FORMATS
ImageAccessState toVulkanAccess(ResourceAccess state)
{
    VkPipelineStageFlags stages = 0;
    if (state.stages & shaderStageMask(ShaderStage::Vertex))
        stages |= VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
    if (state.stages & shaderStageMask(ShaderStage::Fragment))
        stages |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    if (state.stages & shaderStageMask(ShaderStage::Compute))
        stages |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    if (state.stages & ~7u)
        invalid("invalid access shader stages");
    switch (state.usage)
    {
    case AccessUsage::Undefined:
        return {VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0};
    case AccessUsage::CopySource:
        return {VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_ACCESS_TRANSFER_READ_BIT};
    case AccessUsage::CopyDestination:
        return {VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_ACCESS_TRANSFER_WRITE_BIT};
    case AccessUsage::VertexRead:
        return {VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT};
    case AccessUsage::IndexRead:
        return {VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                VK_ACCESS_INDEX_READ_BIT};
    case AccessUsage::UniformRead:
        if (!stages)
            invalid("uniform read requires shader stages");
        return {VK_IMAGE_LAYOUT_UNDEFINED, stages, VK_ACCESS_UNIFORM_READ_BIT};
    case AccessUsage::ShaderRead:
        if (!stages)
            invalid("shader read requires stages");
        return {VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, stages, VK_ACCESS_SHADER_READ_BIT};
    case AccessUsage::ShaderReadWrite:
        if (!stages)
            invalid("shader access requires stages");
        return {VK_IMAGE_LAYOUT_GENERAL, stages,
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
    case AccessUsage::ColorWrite:
        return {VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT};
    case AccessUsage::DepthReadWrite:
        return {VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                    VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT};
    case AccessUsage::Present:
        return {VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0};
    case AccessUsage::HostRead:
        return {VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT};
    case AccessUsage::HostWrite:
        return {VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_WRITE_BIT};
    }
    invalid("invalid resource access");
}
BufferRef VulkanResourceDevice::createBuffer(const BufferDesc &desc) const
{
    if (!desc.size || !desc.usage || (desc.usage & ~63u))
        invalid("invalid buffer description");
    VkBufferUsageFlags usage = 0;
    if (desc.usage & flag(BufferUsage::TransferSource))
        usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (desc.usage & flag(BufferUsage::TransferDestination))
        usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (desc.usage & flag(BufferUsage::Vertex))
        usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (desc.usage & flag(BufferUsage::Index))
        usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (desc.usage & flag(BufferUsage::Uniform))
        usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (desc.usage & flag(BufferUsage::Storage))
        usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    auto result = std::make_shared<BufferOwner>();
    result->device = id_;
    result->desc = desc;
    result->value.create(device_, desc.size, usage, nativeMemory(desc.requiredMemory));
    return result;
}
void VulkanResourceDevice::writeBuffer(const BufferRef &target, const void *bytes, uint64_t size,
                                       uint64_t offset) const
{
    const auto &owner = checked<BufferOwner>(target);
    sameDevice(id_, owner);
    owner.value.write(bytes, size, offset);
}
TextureRef VulkanResourceDevice::createTexture(const TextureDesc &desc) const
{
    if (desc.format == Format::Undefined || !desc.usage || (desc.usage & ~63u) ||
        !desc.extent.width || !desc.extent.height || !desc.extent.depth || !desc.mipLevels ||
        !desc.arrayLayers || !desc.samples || (desc.samples & (desc.samples - 1)) ||
        desc.samples > 64)
        invalid("invalid texture description");
    if ((desc.dimension == TextureDimension::D1 &&
         (desc.extent.height != 1 || desc.extent.depth != 1)) ||
        (desc.dimension == TextureDimension::D2 && desc.extent.depth != 1) ||
        (desc.dimension == TextureDimension::D3 && desc.arrayLayers != 1) ||
        (desc.samples != 1 && (desc.dimension != TextureDimension::D2 || desc.mipLevels != 1)) ||
        (desc.cubeCompatible && (desc.dimension != TextureDimension::D2 ||
                                 desc.extent.width != desc.extent.height || desc.arrayLayers < 6)))
        invalid("texture dimensions, layers and samples are incompatible");
    uint32_t maxMips = 1;
    for (uint32_t size = std::max({desc.extent.width, desc.extent.height, desc.extent.depth});
         size > 1; size >>= 1)
        ++maxMips;
    if (desc.mipLevels > maxMips)
        invalid("texture mip count exceeds its extent");
    Image::CreateInfo info;
    info.extent = {desc.extent.width, desc.extent.height, desc.extent.depth};
    info.mipLevels = desc.mipLevels;
    info.arrayLayers = desc.arrayLayers;
    switch (desc.dimension)
    {
    case TextureDimension::D1:
        info.type = VK_IMAGE_TYPE_1D;
        break;
    case TextureDimension::D2:
        info.type = VK_IMAGE_TYPE_2D;
        break;
    case TextureDimension::D3:
        info.type = VK_IMAGE_TYPE_3D;
        break;
    default:
        invalid("invalid texture dimension");
    }
    info.format = toVulkan(desc.format);
    info.samples = static_cast<VkSampleCountFlagBits>(desc.samples);
    info.memoryProperties = nativeMemory(desc.requiredMemory);
    if (desc.cubeCompatible)
        info.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    if (desc.usage & flag(TextureUsage::TransferSource))
        info.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (desc.usage & flag(TextureUsage::TransferDestination))
        info.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (desc.usage & flag(TextureUsage::Sampled))
        info.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (desc.usage & flag(TextureUsage::ColorAttachment))
        info.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (desc.usage & flag(TextureUsage::DepthStencilAttachment))
        info.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (desc.usage & flag(TextureUsage::Storage))
        info.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    VkFormatFeatureFlags features = 0;
    if (desc.usage & flag(TextureUsage::Sampled))
        features |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    if (desc.usage & flag(TextureUsage::ColorAttachment))
        features |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
    if (desc.usage & flag(TextureUsage::DepthStencilAttachment))
        features |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (desc.usage & flag(TextureUsage::Storage))
        features |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
    (void)device_.findSupportedFormat({info.format}, info.tiling, features);
    auto result = std::make_shared<TextureOwner>();
    result->device = id_;
    result->desc = desc;
    result->value.create(device_, info);
    return result;
}
TextureViewRef VulkanResourceDevice::createTextureView(TextureRef source,
                                                       const TextureViewDesc &desc) const
{
    const auto &image = nativeImage(source);
    sameDevice(id_, *source);
    const auto &range = desc.range;
    const auto &texture = source->description();
    if (range.baseMipLevel >= texture.mipLevels || !range.mipLevels ||
        range.mipLevels > texture.mipLevels - range.baseMipLevel ||
        range.baseArrayLayer >= texture.arrayLayers || !range.arrayLayers ||
        range.arrayLayers > texture.arrayLayers - range.baseArrayLayer)
        invalid("texture view range exceeds allocation");
    if (desc.format != Format::Undefined && desc.format != texture.format)
        throw ResourceError(ResourceErrorCode::Unsupported,
                            "view format reinterpretation requires mutable texture formats");
    ImageView::CreateInfo info;
    info.image = image.get();
    info.format = toVulkan(desc.format == Format::Undefined ? texture.format : desc.format);
    switch (desc.dimension)
    {
    case TextureViewDimension::D1:
        info.type = VK_IMAGE_VIEW_TYPE_1D;
        break;
    case TextureViewDimension::D2:
        info.type = VK_IMAGE_VIEW_TYPE_2D;
        break;
    case TextureViewDimension::D3:
        info.type = VK_IMAGE_VIEW_TYPE_3D;
        break;
    case TextureViewDimension::D1Array:
        info.type = VK_IMAGE_VIEW_TYPE_1D_ARRAY;
        break;
    case TextureViewDimension::D2Array:
        info.type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        break;
    case TextureViewDimension::Cube:
        info.type = VK_IMAGE_VIEW_TYPE_CUBE;
        break;
    case TextureViewDimension::CubeArray:
        info.type = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
        break;
    default:
        invalid("invalid view dimension");
    }
    info.subresourceRange = {toVulkanAspects(range.aspects), range.baseMipLevel, range.mipLevels,
                             range.baseArrayLayer, range.arrayLayers};
    auto result = std::make_shared<ViewOwner>();
    result->device = id_;
    result->desc = desc;
    result->source = std::move(source);
    result->value.create(device_.get(), info);
    return result;
}
SamplerRef VulkanResourceDevice::createSampler(const SamplerDesc &desc) const
{
    if (desc.minLod < 0 || desc.maxLod < desc.minLod)
        invalid("invalid sampler LOD range");
    for (auto filter : {desc.minFilter, desc.magFilter, desc.mipFilter})
        if (filter != Filter::Nearest && filter != Filter::Linear)
            invalid("invalid sampler filter");
    VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    info.minFilter = desc.minFilter == Filter::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
    info.magFilter = desc.magFilter == Filter::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
    info.mipmapMode = desc.mipFilter == Filter::Nearest ? VK_SAMPLER_MIPMAP_MODE_NEAREST
                                                        : VK_SAMPLER_MIPMAP_MODE_LINEAR;
    info.addressModeU = address(desc.addressU);
    info.addressModeV = address(desc.addressV);
    info.addressModeW = address(desc.addressW);
    info.minLod = desc.minLod;
    info.maxLod = desc.maxLod;
    info.mipLodBias = desc.mipLodBias;
    info.maxAnisotropy = 1;
    auto result = std::make_shared<SamplerOwner>();
    result->device = id_;
    result->desc = desc;
    result->value.create(device_.get(), info);
    return result;
}
ShaderRef VulkanResourceDevice::createShader(const ShaderCode &desc) const
{
    const auto stage = toVulkanStages(shaderStageMask(desc.stage));
    if (!stage || (stage & (stage - 1)) || desc.artifact.bytes.empty() || desc.entryPoint.empty())
        invalid("invalid shader code");
    const auto words = spirvWords(desc.artifact);
    auto result = std::make_shared<ShaderOwner>();
    result->device = id_;
    result->desc = desc;
    result->nativeDevice = device_.get();
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = desc.artifact.bytes.size();
    info.pCode = words.data();
    if (vkCreateShaderModule(device_.get(), &info, nullptr, &result->value) != VK_SUCCESS)
        throw ResourceError(ResourceErrorCode::DeviceFailure, "failed to create shader module");
    return result;
}
BindingLayoutRef VulkanResourceDevice::createBindingLayout(const BindingLayoutDesc &desc) const
{
    std::set<uint32_t> used;
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    for (const auto &binding : desc.bindings)
    {
        if (!binding.count || !binding.stages || !used.insert(binding.binding).second)
            invalid("invalid or duplicate layout binding");
        bindings.push_back({binding.binding, nativeBindingType(binding.type), binding.count,
                            toVulkanStages(binding.stages), nullptr});
    }
    auto result = std::make_shared<LayoutOwner>();
    result->device = id_;
    result->desc = desc;
    result->value.create(device_.get(), bindings);
    return result;
}
BindingSetRef VulkanResourceDevice::createBindingSet(const BindingSetDesc &desc) const
{
    if (!desc.layout)
        invalid("binding set requires layout");
    sameDevice(id_, *desc.layout);
    std::map<VkDescriptorType, uint32_t> counts;
    std::map<uint32_t, BindingDesc> bindings;
    uint64_t expectedWrites = 0;
    for (const auto &binding : desc.layout->description().bindings)
    {
        counts[nativeBindingType(binding.type)] += binding.count;
        bindings.emplace(binding.binding, binding);
        expectedWrites += binding.count;
    }
    if (expectedWrites != desc.writes.size())
        invalid("binding set must initialize every declared array element");
    std::set<std::pair<uint32_t, uint32_t>> initialized;
    std::vector<VkDescriptorBufferInfo> bufferInfos(desc.writes.size());
    std::vector<VkDescriptorImageInfo> imageInfos(desc.writes.size());
    std::vector<VkWriteDescriptorSet> writes(desc.writes.size());
    for (std::size_t i = 0; i < desc.writes.size(); ++i)
    {
        const auto &input = desc.writes[i];
        auto found = bindings.find(input.binding);
        if (found == bindings.end() || input.arrayElement >= found->second.count ||
            !initialized.emplace(input.binding, input.arrayElement).second)
            invalid("invalid binding write location");
        const auto type = found->second.type;
        auto &write = writes[i];
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstBinding = input.binding;
        write.dstArrayElement = input.arrayElement;
        write.descriptorType = nativeBindingType(type);
        write.descriptorCount = 1;
        std::visit(
            [&](const auto &resource)
            {
                using T = std::decay_t<decltype(resource)>;
                if constexpr (std::is_same_v<T, BufferBinding>)
                {
                    if (!resource.buffer ||
                        (type != BindingType::UniformBuffer && type != BindingType::StorageBuffer))
                        invalid("buffer binding type mismatch");
                    sameDevice(id_, *resource.buffer);
                    const auto &buffer = nativeBuffer(resource.buffer);
                    const auto size =
                        resource.size ? resource.size
                                      : buffer.size() - std::min(resource.offset, buffer.size());
                    if (resource.offset >= buffer.size() || !size ||
                        size > buffer.size() - resource.offset)
                        invalid("buffer binding range exceeds allocation");
                    const auto usage = type == BindingType::UniformBuffer
                                           ? flag(BufferUsage::Uniform)
                                           : flag(BufferUsage::Storage);
                    if (!(resource.buffer->description().usage & usage))
                        invalid("buffer lacks binding usage");
                    bufferInfos[i] = {buffer.get(), resource.offset, size};
                    write.pBufferInfo = &bufferInfos[i];
                }
                else if constexpr (std::is_same_v<T, TextureBinding>)
                {
                    if (!resource.view || type != BindingType::SampledTexture)
                        invalid("texture binding type mismatch");
                    sameDevice(id_, *resource.view);
                    if (!(resource.view->texture()->description().usage &
                          flag(TextureUsage::Sampled)))
                        invalid("texture lacks sampled usage");
                    imageInfos[i].imageView = nativeTextureView(resource.view);
                    imageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    if (resource.access.usage != AccessUsage::ShaderRead)
                        invalid("sampled binding must declare shader read access");
                    write.pImageInfo = &imageInfos[i];
                }
                else if constexpr (std::is_same_v<T, SamplerRef>)
                {
                    if (!resource || type != BindingType::Sampler)
                        invalid("sampler binding type mismatch");
                    sameDevice(id_, *resource);
                    imageInfos[i].sampler = nativeSampler(resource);
                    write.pImageInfo = &imageInfos[i];
                }
                else
                {
                    if (!resource.view || !resource.sampler ||
                        type != BindingType::CombinedTextureSampler)
                        invalid("combined binding type mismatch");
                    sameDevice(id_, *resource.view);
                    sameDevice(id_, *resource.sampler);
                    if (!(resource.view->texture()->description().usage &
                          flag(TextureUsage::Sampled)) ||
                        resource.access.usage != AccessUsage::ShaderRead)
                        invalid("combined texture requires sampled shader read");
                    imageInfos[i] = {nativeSampler(resource.sampler),
                                     nativeTextureView(resource.view),
                                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
                    write.pImageInfo = &imageInfos[i];
                }
            },
            input.resource);
    }
    std::vector<VkDescriptorPoolSize> sizes;
    for (const auto &count : counts)
        sizes.push_back({count.first, count.second});
    auto result = std::make_shared<SetOwner>();
    result->device = id_;
    result->desc = desc;
    result->pool.create(device_.get(), sizes, 1);
    result->value = result->pool.allocate(nativeBindingLayout(desc.layout)->get(), 1).front();
    for (auto &write : writes)
        write.dstSet = result->value;
    vkUpdateDescriptorSets(device_.get(), static_cast<uint32_t>(writes.size()), writes.data(), 0,
                           nullptr);
    return result;
}
rhi::UploadEnqueueResult VulkanUploadAdapter::tryEnqueue(rhi::UploadRequest &request)
{
    vulkan::UploadRequest converted;
    try
    {
        for (const auto &operation : request.operations)
            std::visit(
                [&](const auto &input)
                {
                    using T = std::decay_t<decltype(input)>;
                    if constexpr (std::is_same_v<T, rhi::BufferUpload>)
                    {
                        vulkan::BufferUpload output;
                        output.destination = std::shared_ptr<const vulkan::Buffer>(
                            input.destination, &nativeBuffer(input.destination));
                        output.destinationOffset = input.destinationOffset;
                        output.source = {input.source.owner, input.source.data, input.source.size};
                        const auto state = toVulkanAccess(input.after);
                        output.finalStages = state.stages;
                        output.finalAccess = state.access;
                        converted.operations.emplace_back(std::move(output));
                    }
                    else
                    {
                        vulkan::ImageUpload output;
                        output.destination = std::shared_ptr<const vulkan::Image>(
                            input.destination, &nativeImage(input.destination));
                        output.source = {input.source.owner, input.source.data, input.source.size};
                        const auto &range = input.range;
                        output.range = {toVulkanAspects(range.aspects), range.baseMipLevel,
                                        range.mipLevels, range.baseArrayLayer, range.arrayLayers};
                        output.before = toVulkanAccess(input.before);
                        output.after = toVulkanAccess(input.after);
                        for (const auto &region : input.regions)
                        {
                            VkBufferImageCopy copy{};
                            copy.bufferOffset = region.sourceOffset;
                            copy.bufferRowLength = region.sourceRowLength;
                            copy.bufferImageHeight = region.sourceImageHeight;
                            copy.imageSubresource = {toVulkanAspects(region.aspects),
                                                     region.mipLevel, region.baseArrayLayer,
                                                     region.arrayLayers};
                            copy.imageOffset = {region.offset.x, region.offset.y, region.offset.z};
                            copy.imageExtent = {region.extent.width, region.extent.height,
                                                region.extent.depth};
                            output.regions.push_back(copy);
                        }
                        converted.operations.emplace_back(std::move(output));
                    }
                },
                operation);
    }
    catch (const std::exception &error)
    {
        return {rhi::UploadEnqueueCode::InvalidRequest, {}, error.what()};
    }
    const auto result = uploads_.tryEnqueue(converted);
    if (result.accepted())
        request.operations.clear();
    return {static_cast<rhi::UploadEnqueueCode>(result.code), {result.ticket.value}, result.error};
}
rhi::UploadStatus VulkanUploadAdapter::query(rhi::UploadTicket ticket) const
{
    const auto result = uploads_.query({ticket.value});
    return {static_cast<rhi::UploadState>(result.state), result.totalBytes, result.submittedBytes,
            result.completedBytes, result.error};
}
} // namespace rubia::rhi::vulkan
