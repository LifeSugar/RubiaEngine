/*
负责资源组合和生命周期
负责资产到GPU的适配
负责描述上传需求
*/
#pragma once

#include "asset/TextureAsset.hpp"
#include "vulkan/Image.hpp"
#include "vulkan/ImageView.hpp"
#include "vulkan/VulkanUploadTypes.hpp"

#include <vulkan/vulkan.h>

namespace rubia::rhi::vulkan
{

/// Vulkan sampling resources created from one source-independent TextureAsset.
class GpuTexture final
{
public:
    /// Source asset and sampling view used to create one GPU texture.
    struct CreateInfo
    {
        const asset::TextureAsset* asset = nullptr;
        VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D;
        VkComponentMapping components{
            VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY};
        VkImageSubresourceRange viewRange{
            VK_IMAGE_ASPECT_COLOR_BIT,
            0,
            VK_REMAINING_MIP_LEVELS,
            0,
            VK_REMAINING_ARRAY_LAYERS};
    };

    GpuTexture() = default;
    ~GpuTexture();

    GpuTexture(const GpuTexture&) = delete;
    GpuTexture& operator=(const GpuTexture&) = delete;
    GpuTexture(GpuTexture&& other) noexcept;
    GpuTexture& operator=(GpuTexture&& other) noexcept;

    // Allocates an unpublished texture; bool() does not imply upload completion.
    void allocate(const Device& device, const CreateInfo& createInfo);
    static UploadRequest makeUploadRequest(std::shared_ptr<GpuTexture> texture,
        std::shared_ptr<const asset::TextureAsset> source);
    void reset() noexcept;

    [[nodiscard]] VkFormat format() const noexcept { return format_; }
    [[nodiscard]] VkImageView view() const noexcept { return view_.get(); }
    [[nodiscard]] VkSampler sampler() const noexcept { return sampler_; }
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return format_ != VK_FORMAT_UNDEFINED &&
            image_ && view_ && sampler_ != VK_NULL_HANDLE;
    }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    Image image_;
    ImageView view_;
    VkSampler sampler_ = VK_NULL_HANDLE;
};

} // namespace rubia::rhi::vulkan
