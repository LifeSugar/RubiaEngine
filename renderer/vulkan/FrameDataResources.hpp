#pragma once
#include "core/FrameBindings.hpp"
#include "vulkan/VulkanResourceDevice.hpp"

namespace rubia::rhi::vulkan
{
/// Compatibility facade for backend tests; the state and ABI live in FrameBindings.
class FrameDataResources final
{
public:
    FrameDataResources() = default;
    FrameDataResources(const Device& device, uint32_t frames, uint32_t objects = 1) { create(device, frames, objects); }
    void create(const Device& device, uint32_t frames, uint32_t objects = 1)
    {
        auto adapter = std::make_unique<VulkanResourceDevice>(device);
        bindings_.create(*adapter, frames, objects);
        device_ = std::move(adapter);
    }
    void reset() noexcept { bindings_.reset(); device_.reset(); }
    void setCameraData(const render::CameraGpuData& data) { bindings_.setCameraData(data); }
    void setCameraData(const render::CameraGpuData* data, uint32_t count) { bindings_.setCameraData(data, count); }
    void setObjectData(const render::ObjectGpuData* data, uint32_t count) { bindings_.setObjectData(data, count); }
    void sync(uint32_t index) { bindings_.sync(index); }
    VkDescriptorSetLayout descriptorSetLayout() const { return descriptorSetLayoutReference()->get(); }
    std::shared_ptr<const DescriptorSetLayoutState> descriptorSetLayoutReference() const
    { return bindings_.bindingLayout() ? nativeBindingLayout(bindings_.bindingLayout()) : nullptr; }
    VkDescriptorSet descriptorSet(uint32_t index) const { return nativeBindingSet(bindings_.bindingSet(index)); }
    const rhi::BindingSetRef& bindingSet(uint32_t index) const { return bindings_.bindingSet(index); }
    uint32_t frameCount() const noexcept { return bindings_.frameCount(); }
    uint32_t objectCapacity() const noexcept { return bindings_.objectCapacity(); }
    static constexpr uint32_t cameraCapacity() noexcept { return render::MaxCameraCount; }
private:
    std::unique_ptr<VulkanResourceDevice> device_;
    render::FrameBindings bindings_;
};
} // namespace rubia::rhi::vulkan
