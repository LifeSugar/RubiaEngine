#pragma once
#include "rhi/Device.hpp"
#include "rhi/UploadService.hpp"
#include "vulkan/Device.hpp"
#include "vulkan/Buffer.hpp"
#include "vulkan/Image.hpp"
#include "vulkan/DescriptorSetLayout.hpp"
#include "vulkan/VulkanUploadService.hpp"

namespace rubia::rhi::vulkan
{
/// Bridges only primitive RHI descriptions; never sees Asset or Material types.
class VulkanResourceDevice final : public rhi::Device
{
  public:
    explicit VulkanResourceDevice(const vulkan::Device &);
    DeviceId id() const noexcept override { return id_; }
    DeviceLimits limits() const noexcept override { return limits_; }
    ClipSpaceConvention clipSpaceConvention() const noexcept override
    {
        ClipSpaceConvention convention;
        convention.correction[5] = -1.0f;
        return convention;
    }
    BufferRef createBuffer(const BufferDesc &) const override;
    void writeBuffer(const BufferRef &, const void *, uint64_t, uint64_t offset = 0) const override;
    TextureRef createTexture(const TextureDesc &) const override;
    TextureViewRef createTextureView(TextureRef, const TextureViewDesc &) const override;
    SamplerRef createSampler(const SamplerDesc &) const override;
    ShaderRef createShader(const ShaderCode &) const override;
    BindingLayoutRef createBindingLayout(const BindingLayoutDesc &) const override;
    BindingSetRef createBindingSet(const BindingSetDesc &) const override;
    const vulkan::Device &native() const noexcept { return device_; }

  private:
    const vulkan::Device &device_;
    DeviceId id_;
    DeviceLimits limits_;
};
const vulkan::Buffer &nativeBuffer(const BufferRef &);
const vulkan::Image &nativeImage(const TextureRef &);
VkImageView nativeTextureView(const TextureViewRef &);
VkSampler nativeSampler(const SamplerRef &);
VkShaderModule nativeShader(const ShaderRef &);
VkDevice nativeShaderDevice(const ShaderRef &);
std::shared_ptr<const DescriptorSetLayoutState> nativeBindingLayout(const BindingLayoutRef &);
BindingLayoutRef borrowBindingLayout(const VulkanResourceDevice &,
                                     std::shared_ptr<const DescriptorSetLayoutState>);
VkDescriptorSet nativeBindingSet(const BindingSetRef &);
VkFormat toVulkan(Format);
Format fromVulkanFormat(VkFormat);
VkShaderStageFlags toVulkanStages(ShaderStageMask);
VkImageAspectFlags toVulkanAspects(uint32_t);
ImageAccessState toVulkanAccess(ResourceAccess);

/// One-to-one ticket adapter over the existing validated Vulkan upload executor.
/// Owns no second queue or ticket registry.
class VulkanUploadAdapter final : public rhi::UploadService
{
  public:
    explicit VulkanUploadAdapter(VulkanUploadService &uploads) : uploads_(uploads) {}
    rhi::UploadEnqueueResult tryEnqueue(rhi::UploadRequest &) override;
    rhi::UploadStatus query(rhi::UploadTicket) const override;
    void cancel(rhi::UploadTicket t) override { uploads_.cancel({t.value}); }
    void releaseTicket(rhi::UploadTicket t) override { uploads_.releaseTicket({t.value}); }
    void tick(const rhi::UploadBudget &b = {}) override
    {
        uploads_.tick({b.maxBytesPerTick, b.maxOperationsPerTick, b.maxCpuTime});
    }
    void drain() override { uploads_.drain(); }
    void shutdown() noexcept override { uploads_.shutdown(); }
    uint64_t stagedBytes() const noexcept override { return uploads_.stagedBytes(); }

  private:
    VulkanUploadService &uploads_;
};
} // namespace rubia::rhi::vulkan
