#pragma once
#include "rhi/Device.hpp"
namespace rubia::render
{
/// Renderer sampling bundle. References pin one exact allocation across replacement.
class TextureResource
{
  public:
    struct CreateInfo
    {
        rhi::TextureDesc image;
        rhi::SamplerDesc sampler;
        rhi::TextureViewDimension viewType = rhi::TextureViewDimension::D2;
        rhi::SubresourceRange viewRange;
    };
    void allocate(const rhi::Device &, const CreateInfo &);
    void reset() noexcept
    {
        sampler_.reset();
        view_.reset();
        image_.reset();
    }
    TextureResource snapshot() const noexcept { return *this; }
    const rhi::TextureRef &image() const noexcept { return image_; }
    rhi::Format format() const noexcept
    {
        return image_ ? image_->description().format : rhi::Format::Undefined;
    }
    const rhi::TextureViewRef &view() const noexcept { return view_; }
    const rhi::SamplerRef &sampler() const noexcept { return sampler_; }
    explicit operator bool() const noexcept { return image_ && view_ && sampler_; }

  private:
    rhi::TextureRef image_;
    rhi::TextureViewRef view_;
    rhi::SamplerRef sampler_;
};
} // namespace rubia::render
