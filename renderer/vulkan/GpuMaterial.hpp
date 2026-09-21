#pragma once

#include "asset/MaterialAsset.hpp"
#include "vulkan/Buffer.hpp"

#include <vulkan/vulkan.h>

#include <vector>
#include <memory>

namespace rubia::asset
{
class MaterialTemplateAsset;
}

namespace rubia::rhi::vulkan
{

class Device;
class GpuTexture;

/// Parameter buffer and texture descriptors compiled for one MaterialAsset.
class GpuMaterial final
{
public:
    GpuMaterial() = default;

    GpuMaterial(const GpuMaterial&) = delete;
    GpuMaterial& operator=(const GpuMaterial&) = delete;
    GpuMaterial(GpuMaterial&&) noexcept = default;
    GpuMaterial& operator=(GpuMaterial&&) noexcept = default;

    /// Creates unpublished bindings around the preparation-owned parameter buffer.
    /// Its bytes must be initialized before publishing or drawing this material.
    void create(
        const Device& device,
        const asset::MaterialAsset& asset,
        const asset::MaterialTemplateAsset& materialTemplate,
        const std::vector<const GpuTexture*>& textures,
        VkDescriptorSet descriptorSet,
        std::shared_ptr<const Buffer> parameterBuffer);
    /// Creates bindings in a NEW, unpublished set using the same material layout,
    /// sharing immutable parameters.
    /// The caller owns the set's pool and retires the previous binding version.
    [[nodiscard]] GpuMaterial withTextures(
        const Device& device,
        const asset::MaterialTemplateAsset& materialTemplate,
        const std::vector<const GpuTexture*>& textures,
        VkDescriptorSet descriptorSet) const;
    void reset() noexcept;

    [[nodiscard]] VkDescriptorSet descriptorSet() const noexcept
    {
        return descriptorSet_;
    }
    [[nodiscard]] VkBuffer parameterBuffer() const noexcept
    {
        return parameterBuffer_ ? parameterBuffer_->get() : VK_NULL_HANDLE;
    }
    /// Keeps the destination alive independently of the unpublished material.
    [[nodiscard]] const std::shared_ptr<const Buffer>& parameterBufferResource() const noexcept
    {
        return parameterBuffer_;
    }
    [[nodiscard]] const asset::MaterialRenderState& renderState() const noexcept
    {
        return renderState_;
    }
    [[nodiscard]] asset::MaterialTemplateAssetHandle materialTemplate() const
        noexcept
    {
        return materialTemplate_;
    }
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return parameterBuffer_ && *parameterBuffer_ && materialTemplate_ &&
            descriptorSet_ != VK_NULL_HANDLE;
    }

private:
    // Published parameter bytes are immutable; texture binding versions share
    // the allocation. A parameter edit creates a new buffer.
    std::shared_ptr<const Buffer> parameterBuffer_;
    asset::MaterialTemplateAssetHandle materialTemplate_;
    asset::MaterialRenderState renderState_;
    VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;
};

} // namespace rubia::rhi::vulkan
