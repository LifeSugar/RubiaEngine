#include "vulkan/VulkanPresentation.hpp"
#include "vulkan/VulkanResourceDevice.hpp"
#include "vulkan/Device.hpp"
#include <algorithm>
#include <cassert>
#include <stdexcept>

namespace rubia::rhi::vulkan
{
struct VulkanPresentation::Frame final : AcquiredFrame
{
    enum class State { Acquired, Recording, Recorded, Submitted, Presented };
    Frame(VulkanPresentation& owner, uint32_t slot)
        : owner(owner), slot(slot), description(owner.output_),
          pass(owner.deviceId_, owner.resources_.renderPassReference(), VK_NULL_HANDLE,
              owner.resources_.extent()),
          commands(owner.deviceId_, owner.slots_.at(slot).commandBuffer()) {}
    ~Frame() override
    {
        if (owner.active_ == this)
        {
            // Vulkan has no portable release of an acquired image/semaphore.
            // Do not reuse the slot: configure() rebuilds it after device idle.
            if (state != State::Presented) owner.failed_ = true;
            owner.active_ = nullptr;
        }
    }
    const PassTarget& target() const noexcept override { return pass; }
    const OutputDescription& output() const noexcept override { return description; }
    VulkanPresentation& owner;
    uint32_t slot, image = 0;
    OutputDescription description;
    VulkanPassTarget pass;
    VulkanCommandEncoder commands;
    State state = State::Acquired;
};

VulkanPresentation::VulkanPresentation(const Device& device, DeviceId id,
    VkSurfaceKHR surface, uint32_t count) : device_(device), deviceId_(id), surface_(surface)
{
    if (!device || !id || !surface || !count)
        throw std::invalid_argument("invalid presentation device, surface or slot count");
    slots_.reserve(count);
    for (uint32_t i = 0; i < count; ++i) slots_.emplace_back(device_);
    prepared_.resize(count, false);
}
VulkanPresentation::~VulkanPresentation()
{
    assert(!active_);
    // Also protects abandoned frames whose graphics submission already succeeded.
    try { device_.waitIdle(); } catch (...) {}
}
void VulkanPresentation::configure(const PresentationConfig& config)
{
    if (active_) throw std::logic_error("cannot configure a leased presentation surface");
    // Enforce idle here too, including on recovery from an unsignaled slot fence.
    device_.waitIdle();
    resetCommandsAfterIdle();
    if (!config.width || !config.height)
    {
        output_.width = output_.height = 0;
        return;
    }
    failed_ = true;
    std::vector<FrameContext> replacement;
    replacement.reserve(slots_.size());
    for (size_t i = 0; i < slots_.size(); ++i) replacement.emplace_back(device_);
    SwapchainResources::CreateInfo info;
    info.surface = surface_;
    info.framebufferExtent = {config.width, config.height};
    info.presentation = config;
    try
    {
        static_cast<void>(resources_.recreate(device_, info));
    }
    catch (...)
    {
        // Creating the replacement swapchain may already have retired the old
        // one before framebuffer allocation failed. Never retry that old handle.
        resources_.reset();
        output_ = {};
        throw;
    }
    // Old per-image fence references were destroyed by recreate, before slots.
    slots_ = std::move(replacement);
    const auto extent = resources_.extent();
    output_ = {extent.width, extent.height, fromVulkanFormat(resources_.format()),
        resources_.colorSpace() == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
            ? OutputColorSpace::SrgbNonlinear : OutputColorSpace::Unknown,
        resources_.presentMode()};
    failed_ = false;
}
void VulkanPresentation::prepareSlot(uint32_t slot)
{
    if (failed_) throw std::logic_error("presentation requires configure after an abandoned or failed frame");
    if (active_) throw std::logic_error("presentation already has an acquired frame");
    auto& context = slots_.at(slot);
    context.waitUntilReusable();
    context.resetCommands();
    prepared_.at(slot) = true;
}
AcquireResult VulkanPresentation::acquire(uint32_t slot)
{
    if (failed_ || active_ || !prepared_.at(slot))
        throw std::logic_error("acquire requires a prepared presentation slot");
    prepared_[slot] = false;
    if (!output_.width || !output_.height) return {PresentationStatus::Unavailable, {}};
    // Allocate before signaling a binary semaphore, so allocation failure is harmless.
    auto frame = std::make_unique<Frame>(*this, slot);
    const auto result = resources_.acquireNextImage(device_, slots_[slot].imageAvailable(), frame->image);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) return {PresentationStatus::NeedsReconfigure, {}};
    if (result == VK_ERROR_SURFACE_LOST_KHR)
    {
        failed_ = true;
        return {PresentationStatus::SurfaceLost, {}};
    }
    if (result == VK_NOT_READY || result == VK_TIMEOUT) return {PresentationStatus::Unavailable, {}};
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    {
        failed_ = true;
        throw std::runtime_error("failed to acquire presentation image");
    }
    active_ = frame.get();
    resources_.waitUntilImageReusable(device_, frame->image);
    frame->pass = VulkanPassTarget(deviceId_, resources_.renderPassReference(),
        resources_.framebuffer(frame->image), resources_.extent());
    return {result == VK_SUBOPTIMAL_KHR ? PresentationStatus::Suboptimal : PresentationStatus::Ready,
        std::move(frame)};
}
VulkanPresentation::Frame& VulkanPresentation::checked(AcquiredFrame& frame)
{
    auto* native = dynamic_cast<Frame*>(&frame);
    if (!native || native != active_ || &native->owner != this || failed_)
        throw std::invalid_argument("frame does not belong to this active presentation session");
    return *native;
}
VkCommandBuffer VulkanPresentation::commandBuffer(AcquiredFrame& frame)
{
    return slots_[checked(frame).slot].commandBuffer();
}
CommandEncoder& VulkanPresentation::beginCommands(AcquiredFrame& lease)
{
    auto& frame = checked(lease);
    if (frame.state != Frame::State::Acquired) throw std::logic_error("frame already recorded");
    VkCommandBufferBeginInfo info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(slots_[frame.slot].commandBuffer(), &info) != VK_SUCCESS)
        throw std::runtime_error("failed to begin presentation commands");
    frame.state = Frame::State::Recording;
    return frame.commands;
}
void VulkanPresentation::endCommands(AcquiredFrame& lease)
{
    auto& frame = checked(lease);
    if (frame.state != Frame::State::Recording) throw std::logic_error("frame is not recording");
    if (vkEndCommandBuffer(slots_[frame.slot].commandBuffer()) != VK_SUCCESS)
        throw std::runtime_error("failed to finish presentation commands");
    frame.state = Frame::State::Recorded;
}
void VulkanPresentation::submit(AcquiredFrame& lease)
{
    auto& frame = checked(lease);
    if (frame.state != Frame::State::Recorded) throw std::logic_error("frame is not ready for submission");
    auto& slot = slots_[frame.slot];
    const VkSemaphore wait = slot.imageAvailable();
    const VkSemaphore signal = resources_.renderFinished(frame.image);
    const VkCommandBuffer commands = slot.commandBuffer();
    const VkPipelineStageFlags stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    // Validate/update the image association before submitting; nothing after a
    // successful submit may throw before the caller commits its lifetime ledger.
    resources_.markImageInFlight(frame.image, slot.inFlightFence());
    slot.resetFence();
    VkSubmitInfo info{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores = &wait;
    info.pWaitDstStageMask = &stage;
    info.commandBufferCount = 1;
    info.pCommandBuffers = &commands;
    info.signalSemaphoreCount = 1;
    info.pSignalSemaphores = &signal;
    if (vkQueueSubmit(device_.graphicsQueue(), 1, &info, slot.inFlightFence()) != VK_SUCCESS)
        throw std::runtime_error("failed to submit presentation commands");
    frame.state = Frame::State::Submitted;
}
PresentationStatus VulkanPresentation::present(AcquiredFrame& lease)
{
    auto& frame = checked(lease);
    if (frame.state != Frame::State::Submitted) throw std::logic_error("frame has not been submitted");
    const auto result = resources_.present(device_.presentQueue(), frame.image);
    if (result == VK_ERROR_SURFACE_LOST_KHR)
    {
        failed_ = true;
        return PresentationStatus::SurfaceLost;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR && result != VK_ERROR_OUT_OF_DATE_KHR)
        throw std::runtime_error("failed to present frame");
    frame.state = Frame::State::Presented;
    if (result == VK_ERROR_OUT_OF_DATE_KHR) return PresentationStatus::NeedsReconfigure;
    return result == VK_SUBOPTIMAL_KHR ? PresentationStatus::Suboptimal : PresentationStatus::Ready;
}
void VulkanPresentation::resetCommandsAfterIdle()
{
    if (active_) throw std::logic_error("cannot reset commands of a leased frame");
    for (auto& slot : slots_) slot.resetCommands();
    std::fill(prepared_.begin(), prepared_.end(), false);
}
}
