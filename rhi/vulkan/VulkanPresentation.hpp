#pragma once
#include "rhi/Presentation.hpp"
#include "vulkan/FrameContext.hpp"
#include "vulkan/SwapchainResources.hpp"
#include "vulkan/VulkanCommandEncoder.hpp"

namespace rubia::rhi::vulkan
{
// Vulkan-only composition/GUI adapter. Generic frame orchestration uses the two
// RHI interfaces; swapchain images, binary semaphores and fences stay here.
class VulkanPresentation final : public PresentationSurface, public FrameExecutor
{
public:
    VulkanPresentation(const Device&, DeviceId, VkSurfaceKHR, uint32_t slots);
    ~VulkanPresentation() override;
    void configure(const PresentationConfig&) override;
    OutputDescription output() const noexcept override { return output_; }
    AcquireResult acquire(uint32_t slot) override;
    uint32_t slotCount() const noexcept override { return static_cast<uint32_t>(slots_.size()); }
    void prepareSlot(uint32_t slot) override;
    CommandEncoder& beginCommands(AcquiredFrame&) override;
    void endCommands(AcquiredFrame&) override;
    void submit(AcquiredFrame&) override;
    PresentationStatus present(AcquiredFrame&) override;
    void resetCommandsAfterIdle() override;
    bool failed() const noexcept { return failed_; }

    // Native interoperability required by the existing Vulkan pipeline/ImGui adapters.
    VkCommandBuffer commandBuffer(AcquiredFrame&);
    VkRenderPass renderPass() const noexcept { return resources_.renderPass(); }
    std::shared_ptr<const RenderPassState> renderPassReference() const noexcept { return resources_.renderPassReference(); }
    uint32_t imageCount() const noexcept { return static_cast<uint32_t>(resources_.imageCount()); }
private:
    struct Frame;
    Frame& checked(AcquiredFrame&);
    const Device& device_;
    DeviceId deviceId_;
    VkSurfaceKHR surface_;
    SwapchainResources resources_;
    std::vector<FrameContext> slots_;
    std::vector<bool> prepared_;
    OutputDescription output_;
    Frame* active_ = nullptr;
    bool failed_ = false;
};
}
