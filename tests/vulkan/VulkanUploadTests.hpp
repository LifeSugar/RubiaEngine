#pragma once
#include "asset/AssetFwd.hpp"
namespace rubia::asset
{
class AssetManager;
}
namespace rubia::rhi::vulkan
{
class Device;
}
namespace rubia::test
{
void runAssetPreparationTests(const rhi::vulkan::Device& device, asset::AssetManager& assets,
                              asset::ModelAssetHandle model,
                              asset::ShaderProgramAssetHandle present);
void runVulkanUploadTests(const rhi::vulkan::Device& device);
} // namespace rubia::test
