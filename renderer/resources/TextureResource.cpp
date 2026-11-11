#include "resources/TextureResource.hpp"
#include <algorithm>
namespace rubia::render
{
void TextureResource::allocate(const rhi::Device &device, const CreateInfo &info)
{
    TextureResource replacement;
    replacement.image_ = device.createTexture(info.image);
    replacement.view_ = device.createTextureView(
        replacement.image_, {info.viewType, info.image.format, info.viewRange});
    auto sampler = info.sampler;
    sampler.maxLod = std::min(sampler.maxLod, static_cast<float>(info.viewRange.mipLevels - 1));
    replacement.sampler_ = device.createSampler(sampler);
    *this = std::move(replacement);
}
} // namespace rubia::render
