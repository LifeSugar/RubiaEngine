#pragma once
#include "render/RenderData.hpp"
#include "rhi/Device.hpp"

namespace rubia::render
{
/// Camera/Object ABI and per-frame replicas belong to the Renderer. Call sync
/// only after the submission using that slot has completed.
class FrameBindings final
{
public:
    void create(const rhi::Device&, uint32_t frameCount, uint32_t objectCapacity = 1);
    void reset() noexcept;
    /// Accept logical camera snapshots. This applies the Device clip-space
    /// correction once; callers retain their unchanged CPU culling matrices.
    void setCameraData(const CameraGpuData& data) { setCameraData(&data, 1); }
    void setCameraData(const CameraGpuData*, uint32_t count);
    void setObjectData(const ObjectGpuData*, uint32_t count);
    void sync(uint32_t frameIndex);
    const rhi::BindingLayoutRef& bindingLayout() const noexcept { return layout_; }
    const rhi::BindingSetRef& bindingSet(uint32_t index) const { return slots_.at(index).bindings; }
    uint32_t frameCount() const noexcept { return static_cast<uint32_t>(slots_.size()); }
    uint32_t objectCapacity() const noexcept { return objectCapacity_; }
    static constexpr uint32_t cameraCapacity() noexcept { return MaxCameraCount; }
private:
    struct Slot
    {
        rhi::BufferRef cameras, objects;
        rhi::BindingSetRef bindings;
        bool cameraDirty = false, objectDirty = false;
    };
    const rhi::Device* device_ = nullptr;
    glm::mat4 clipSpaceCorrection_{1.0f};
    rhi::BindingLayoutRef layout_;
    std::vector<Slot> slots_;
    std::vector<CameraGpuData> cameras_;
    std::vector<ObjectGpuData> objects_;
    uint32_t objectCapacity_ = 0;
};
} // namespace rubia::render
