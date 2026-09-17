/*
负责资源组合和生命周期
资产适配和完整纹理上传策略位于 TextureUploadBuilder
*/
#pragma once

#include "vulkan/Image.hpp"
#include "vulkan/ImageView.hpp"

#include <vulkan/vulkan.h>

namespace rubia::rhi::vulkan
{

/// Owns an image, sampling view and sampler; no asset or upload policy.
class GpuTexture final
{
public:
    /// Backend resource descriptions used to create one GPU texture.
    struct CreateInfo
    {
        Image::CreateInfo image;
        VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
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
    void reset() noexcept;

    [[nodiscard]] const Image& image() const noexcept
    {
        return image_;
    }
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
