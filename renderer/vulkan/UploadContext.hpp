#pragma once

#include "vulkan/CommandPool.hpp"
#include "vulkan/Device.hpp"
#include "vulkan/VulkanUploadTypes.hpp"

#include <vector>

namespace rubia::rhi::vulkan
{
// Backend batch recorder. Service/caller retains immutable sources and targets;
// this context owns staging memory, one command buffer and its completion fence.
class UploadContext final
{
public:
    UploadContext(const Device& device, CommandPool& commandPool);
    ~UploadContext();
    UploadContext(const UploadContext&) = delete;
    UploadContext& operator=(const UploadContext&) = delete;

    void beginBatch();
    void submitBatch();
    [[nodiscard]] bool pollBatch();
    void waitBatch();
    // Discards unsubmitted commands; waits before releasing submitted resources.
    void discardBatch() noexcept;
    [[nodiscard]] VkDeviceSize stagedByteCount() const noexcept
    {
        return stagedBytes_;
    }

    // Same description and validation for queued requests and command recording.
    static void validateBufferUpload(const BufferUpload& upload);
    static void validateImageUpload(const ImageUpload& upload);
    // Recording only; an explicit batch must be open. Caller owns rollback.
    void recordBufferUpload(const BufferUpload& upload);
    void recordImageUpload(const ImageUpload& upload);

private:
    /// 借用 VKDevice，必须比当前批次和 Context 活得更久。
    const Device* device_ = nullptr;
    /// 借用 cmdPool；所有录制与提交由服务所属线程执行。
    CommandPool* commandPool_ = nullptr;
    VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;
    bool submitted_ = false;  //命理已提交；但是还没有完成他的回收
    std::vector<Buffer> stagingBuffers_;
    VkDeviceSize stagedBytes_ = 0;
};
} // namespace rubia::rhi::vulkan
