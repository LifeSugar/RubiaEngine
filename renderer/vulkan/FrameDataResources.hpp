#pragma once

#include "vulkan/DescriptorPool.hpp"
#include "vulkan/DescriptorSetLayout.hpp"
#include "vulkan/PerFrameBuffer.hpp"
#include "render/RenderData.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace rubia::rhi::vulkan
{

class Device;

/// Owns per-frame GPU data buffers and their descriptor sets.
class FrameDataResources final
{
public:
    /// Creates an empty frame-data resource set.
    FrameDataResources() = default;
    /// Creates descriptor and buffer resources for all frame slots.
    FrameDataResources(
        const Device& device,
        uint32_t frameCount,
        uint32_t objectCapacity = 1);
    /// Releases all frame-data resources.
    ~FrameDataResources();

    FrameDataResources(const FrameDataResources&) = delete;
    FrameDataResources& operator=(const FrameDataResources&) = delete;
    FrameDataResources(FrameDataResources&&) = delete;
    FrameDataResources& operator=(FrameDataResources&&) = delete;

    /// Creates or replaces descriptors and per-frame buffers.
    void create(
        const Device& device,
        uint32_t frameCount,
        uint32_t objectCapacity = 1);
    /// Releases all descriptor and buffer resources.
    void reset() noexcept;

    /// Stages the latest camera snapshot for all frame replicas.
    void setCameraData(const render::CameraGpuData& cameraData);
    /// Stages an array of camera snapshots for all frame replicas.
    void setCameraData(const render::CameraGpuData* cameraData, uint32_t cameraCount);
    /// Stages the active object-data range for all frame replicas.
    void setObjectData(const render::ObjectGpuData* objectData, uint32_t objectCount);
    /// Uploads stale camera and object data for one frame slot.
    void sync(uint32_t frameIndex);

    /// Returns the layout shared by every frame descriptor set.
    [[nodiscard]] VkDescriptorSetLayout descriptorSetLayout() const noexcept
    {
        return descriptorSetLayout_.get();
    }
    std::shared_ptr<const DescriptorSetLayoutState> descriptorSetLayoutReference() const noexcept
    {
        return descriptorSetLayout_.reference();
    }
    /// Returns the descriptor set assigned to one frame slot.
    [[nodiscard]] VkDescriptorSet descriptorSet(uint32_t frameIndex) const;
    /// Returns the number of allocated frame slots.
    [[nodiscard]] uint32_t frameCount() const noexcept
    {
        return static_cast<uint32_t>(descriptorSets_.size());
    }
    /// Returns the maximum number of object records per frame.
    [[nodiscard]] uint32_t objectCapacity() const noexcept
    {
        return objectCapacity_;
    }
    /// Returns the maximum number of camera records per frame.
    [[nodiscard]] static constexpr uint32_t cameraCapacity() noexcept
    {
        return render::MaxCameraCount;
    }

private:
    // Declaration order keeps the implicit destruction order safe:
    // pool -> descriptor handles -> buffers -> layout.
    /// Layout describing the camera and object buffer bindings.
    DescriptorSetLayout descriptorSetLayout_;
    /// Uniform-buffer replica containing camera data for each frame slot.
    PerFrameBuffer cameraBuffers_;
    /// Storage-buffer replica containing object data for each frame slot.
    PerFrameBuffer objectBuffers_;
    /// Descriptor set assigned to each frame slot.
    std::vector<VkDescriptorSet> descriptorSets_;
    /// Pool that owns all frame descriptor sets.
    DescriptorPool descriptorPool_;
    /// Maximum number of object records stored in each object buffer.
    uint32_t objectCapacity_ = 0;
};

} // namespace rubia::rhi::vulkan
