#pragma once
#include "asset/TextureAsset.hpp"
#include "resources/TextureResource.hpp"
#include "rhi/UploadService.hpp"
namespace rubia::render
{
rhi::Format textureFormat(asset::TextureFormat, asset::TextureColorSpace);
TextureResource::CreateInfo makeTextureCreateInfo(const asset::TextureAsset &);
rhi::UploadRequest makeTextureUploadRequest(std::shared_ptr<TextureResource>,
                                            std::shared_ptr<const asset::TextureAsset>);
} // namespace rubia::render
