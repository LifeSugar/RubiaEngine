#include "core/FrameBindings.hpp"
#include <stdexcept>

namespace rubia::render
{
void FrameBindings::create(const rhi::Device& device, uint32_t frameCount, uint32_t objectCapacity)
{
    if (!device.id() || !frameCount || !objectCapacity)
        throw std::invalid_argument("frame bindings require a device and nonempty capacity");
    const auto vertex = rhi::shaderStageMask(rhi::ShaderStage::Vertex);
    const auto fragment = rhi::shaderStageMask(rhi::ShaderStage::Fragment);
    auto layout = device.createBindingLayout({{
        {1, rhi::BindingType::UniformBuffer, 1, vertex | fragment},
        {2, rhi::BindingType::StorageBuffer, 1, vertex}}});
    const uint64_t cameraBytes = uint64_t{sizeof(CameraGpuData)} * MaxCameraCount;
    const uint64_t objectBytes = uint64_t{sizeof(ObjectGpuData)} * objectCapacity;
    const auto memory = rhi::flag(rhi::MemoryProperty::HostVisible) |
                        rhi::flag(rhi::MemoryProperty::HostCoherent);
    std::vector<Slot> slots;
    slots.reserve(frameCount);
    for (uint32_t i = 0; i < frameCount; ++i)
    {
        Slot slot;
        slot.cameras = device.createBuffer({cameraBytes, rhi::flag(rhi::BufferUsage::Uniform), memory});
        slot.objects = device.createBuffer({objectBytes, rhi::flag(rhi::BufferUsage::Storage), memory});
        slot.bindings = device.createBindingSet({layout, {
            {1, 0, rhi::BufferBinding{slot.cameras, 0, cameraBytes}},
            {2, 0, rhi::BufferBinding{slot.objects, 0, objectBytes}}}});
        slots.push_back(std::move(slot));
    }
    // Allocation failure preserves the previous complete set of replicas.
    reset();
    device_ = &device;
    const auto convention = device.clipSpaceConvention();
    for (glm::length_t column = 0; column < 4; ++column)
        for (glm::length_t row = 0; row < 4; ++row)
            clipSpaceCorrection_[column][row] = convention.correction[column * 4 + row];
    layout_ = std::move(layout);
    slots_ = std::move(slots);
    objectCapacity_ = objectCapacity;
}
void FrameBindings::reset() noexcept
{
    slots_.clear();
    layout_.reset();
    cameras_.clear();
    objects_.clear();
    objectCapacity_ = 0;
    device_ = nullptr;
    clipSpaceCorrection_ = glm::mat4{1.0f};
}
void FrameBindings::setCameraData(const CameraGpuData* data, uint32_t count)
{
    if (!device_ || !data || !count || count > MaxCameraCount)
        throw std::invalid_argument("camera data exceeds the frame buffer capacity");
    cameras_.assign(data, data + count);
    // Correct the whole VP, including rotation and translation. Logical
    // snapshots stay unchanged for CPU culling and screen-space operations.
    for (auto& camera : cameras_)
        camera.viewProjection = clipSpaceCorrection_ * camera.viewProjection;
    for (auto& slot : slots_) slot.cameraDirty = true;
}
void FrameBindings::setObjectData(const ObjectGpuData* data, uint32_t count)
{
    if (!device_ || !data || !count || count > objectCapacity_)
        throw std::invalid_argument("object data exceeds the frame buffer capacity");
    objects_.assign(data, data + count);
    for (auto& slot : slots_) slot.objectDirty = true;
}
void FrameBindings::sync(uint32_t index)
{
    auto& slot = slots_.at(index);
    if (slot.cameraDirty)
    {
        device_->writeBuffer(slot.cameras, cameras_.data(), cameras_.size() * sizeof(CameraGpuData));
        slot.cameraDirty = false;
    }
    if (slot.objectDirty)
    {
        device_->writeBuffer(slot.objects, objects_.data(), objects_.size() * sizeof(ObjectGpuData));
        slot.objectDirty = false;
    }
}
} // namespace rubia::render
