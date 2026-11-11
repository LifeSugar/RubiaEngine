#pragma once
#include "resources/ResourcePreparationService.hpp"
#include "vulkan/RenderAssetCache.hpp"
#include "vulkan/IResourcePreparation.hpp"
#include "vulkan/ResourcePreparationTypes.hpp"
#include "vulkan/RetiredResources.hpp"
#include "vulkan/VulkanResourceDevice.hpp"
namespace rubia::rhi::vulkan
{
namespace detail {
// Backend compatibility construction only. The inherited generic service owns
// the sole task/subscriber registries; adapters do not register tasks themselves.
struct PreparationPorts {
    std::unique_ptr<VulkanResourceDevice> ownedDevice;
    std::unique_ptr<VulkanUploadAdapter> ownedUploads;
    const rhi::Device* device;
    rhi::UploadService* uploads;
    PreparationPorts(const vulkan::Device& d,VulkanUploadService& u)
        :ownedDevice(std::make_unique<VulkanResourceDevice>(d)),ownedUploads(std::make_unique<VulkanUploadAdapter>(u)),device(ownedDevice.get()),uploads(ownedUploads.get()){}
    PreparationPorts(const rhi::Device& d,rhi::UploadService& u):device(&d),uploads(&u){}
};
}
class VulkanResourcePreparation final : private detail::PreparationPorts, public render::ResourcePreparationService
{
public:
    VulkanResourcePreparation(const vulkan::Device& device,VulkanUploadService& uploads,RetiredResources& retired,render::ResourcePreparationOptions options={},std::size_t maxRequests=1024)
        :detail::PreparationPorts(device,uploads),render::ResourcePreparationService(*detail::PreparationPorts::device,*detail::PreparationPorts::uploads,retired,options,maxRequests){}
    VulkanResourcePreparation(const rhi::Device& device,rhi::UploadService& uploads,RetiredResources& retired,render::ResourcePreparationOptions options={},std::size_t maxRequests=1024)
        :detail::PreparationPorts(device,uploads),render::ResourcePreparationService(device,uploads,retired,options,maxRequests){}
};
}
