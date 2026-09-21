#include "vulkan/GpuTexture.hpp"

#include "vulkan/Device.hpp"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <utility>

namespace rubia::rhi::vulkan
{
namespace
{

VkImageSubresourceRange resolveViewRange(VkImageSubresourceRange range, uint32_t mipLevels,
                                         uint32_t arrayLayers)
{
    if (range.aspectMask == 0 || range.baseMipLevel >= mipLevels ||
        range.baseArrayLayer >= arrayLayers)
    {
        throw std::invalid_argument("GpuTexture view range starts outside the image");
    }

    if (range.levelCount == VK_REMAINING_MIP_LEVELS)
    {
        range.levelCount = mipLevels - range.baseMipLevel;
    }
    if (range.layerCount == VK_REMAINING_ARRAY_LAYERS)
    {
        range.layerCount = arrayLayers - range.baseArrayLayer;
    }
    if (range.levelCount == 0 || range.levelCount > mipLevels - range.baseMipLevel ||
        range.layerCount == 0 || range.layerCount > arrayLayers - range.baseArrayLayer)
    {
        throw std::invalid_argument("GpuTexture view range exceeds the image");
    }
    return range;
}

} // namespace

struct GpuTexture::State
{
    VkDevice device_ = VK_NULL_HANDLE;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    Image image_;
    ImageView view_;
    VkSampler sampler_ = VK_NULL_HANDLE;
    ~State()
    {
        if (sampler_)
            vkDestroySampler(device_, sampler_, nullptr);
        // Member destruction releases view before image.
    }
};
GpuTexture::~GpuTexture() = default;
GpuTexture::GpuTexture(GpuTexture &&) noexcept = default;
GpuTexture &GpuTexture::operator=(GpuTexture &&) noexcept = default;
GpuTexture GpuTexture::snapshot() const noexcept
{
    GpuTexture result;
    result.state_ = state_;
    return result;
}
const Image &GpuTexture::image() const noexcept
{
    static const Image empty;
    return state_ ? state_->image_ : empty;
}
VkFormat GpuTexture::format() const noexcept
{
    return state_ ? state_->format_ : VK_FORMAT_UNDEFINED;
}
VkImageView GpuTexture::view() const noexcept
{
    return state_ ? state_->view_.get() : VK_NULL_HANDLE;
}
VkSampler GpuTexture::sampler() const noexcept
{
    return state_ ? state_->sampler_ : VK_NULL_HANDLE;
}
GpuTexture::operator bool() const noexcept
{
    return state_ && state_->format_ != VK_FORMAT_UNDEFINED && state_->image_ && state_->view_ &&
           state_->sampler_;
}

void GpuTexture::allocate(const Device &device, const CreateInfo &createInfo)
{
    if (!device || createInfo.image.format == VK_FORMAT_UNDEFINED ||
        createInfo.sampler.sType != VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO)
    {
        throw std::invalid_argument("cannot create GpuTexture from incomplete inputs");
    }
    const auto &imageInfo = createInfo.image;
    const VkFormat format = device.findSupportedFormat({imageInfo.format}, imageInfo.tiling,
                                                       VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
    auto replacement = std::make_shared<State>();
    replacement->device_ = device.get();
    replacement->format_ = format;
    replacement->image_.create(device, imageInfo);

    const VkImageSubresourceRange viewRange =
        resolveViewRange(createInfo.viewRange, imageInfo.mipLevels, imageInfo.arrayLayers);

    ImageView::CreateInfo viewInfo{};
    viewInfo.image = replacement->image_.get();
    viewInfo.type = createInfo.viewType;
    viewInfo.format = format;
    viewInfo.components = createInfo.components;
    viewInfo.subresourceRange = viewRange;
    replacement->view_.create(device.get(), viewInfo);

    VkSamplerCreateInfo samplerInfo = createInfo.sampler;
    samplerInfo.maxLod = std::min(samplerInfo.maxLod, static_cast<float>(viewRange.levelCount - 1));
    if (vkCreateSampler(device.get(), &samplerInfo, nullptr, &replacement->sampler_) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to create a texture sampler");
    }

    state_ = std::move(replacement);
}

void GpuTexture::reset() noexcept
{
    state_.reset();
}
} // namespace rubia::rhi::vulkan
