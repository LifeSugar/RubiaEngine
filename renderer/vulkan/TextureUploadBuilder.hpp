#pragma once
#include "asset/TextureAsset.hpp"
#include "vulkan/GpuTexture.hpp"
#include "vulkan/VulkanUploadTypes.hpp"

namespace rubia::rhi::vulkan
{
// Asset adapter for newly allocated, unpublished full 2D textures.
GpuTexture::CreateInfo makeTextureCreateInfo(const asset::TextureAsset& source);
// Initializes ALL mip levels, discards old contents, ends in fragment shader sampling state.
// For partial updates, construct ImageUpload with explicit before/after states instead.
UploadRequest makeTextureUploadRequest(std::shared_ptr<GpuTexture> texture,
                                       std::shared_ptr<const asset::TextureAsset> source);
} // namespace rubia::rhi::vulkan
