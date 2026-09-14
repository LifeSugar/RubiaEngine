#pragma once

#include "vulkan/Buffer.hpp"
#include "vulkan/CommandPool.hpp"
#include "vulkan/Device.hpp"

#include <vector>

namespace rubia::rhi::vulkan
{

    /// Staging uploads; explicit batches submit without blocking the frame loop.
    class UploadContext final
    {
    public:
        /// Identifies an existing image and the bounds used to validate copies.
        struct ImageDestination
        {
            VkImage image = VK_NULL_HANDLE;
            VkFormat format = VK_FORMAT_UNDEFINED;
            VkExtent3D extent{};
            uint32_t mipLevels = 0;
            uint32_t arrayLayers = 0;
        };

        /// Describes one upload into an existing image.
        struct ImageUploadInfo
        {
            const void* sourceData = nullptr;
            VkDeviceSize sourceSize = 0;
            ImageDestination destination;
            std::vector<VkBufferImageCopy> copyRegions;
            VkImageSubresourceRange destinationRange{};
            VkImageLayout oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            VkPipelineStageFlags sourceStageMask =
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            VkAccessFlags sourceAccessMask = 0;
            VkImageLayout finalLayout =
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            VkPipelineStageFlags finalStageMask =
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            VkAccessFlags finalAccessMask = VK_ACCESS_SHADER_READ_BIT;
        };

        struct BufferUploadInfo
        {
            const Buffer* destination = nullptr;
            VkDeviceSize destinationOffset = 0;

            const void* sourceData = nullptr;
            VkDeviceSize sourceSize = 0;

            VkPipelineStageFlags finalStages = 0;
            VkAccessFlags finalAccess = 0;
        };

        /// Uploads are synchronous unless the caller explicitly starts a batch.
        UploadContext(const Device &device, CommandPool &commandPool);
        ~UploadContext();
        UploadContext(const UploadContext&) = delete;
        UploadContext& operator=(const UploadContext&) = delete;

        /// Record several uploads, retaining staging memory until the fence signals.
        /// Destination resources must outlive the submitted batch. Queue submission
        /// and polling are performed by the render thread.
        void beginBatch();
        void submitBatch();
        [[nodiscard]] bool pollBatch();
        void waitBatch();
        /// Waits for submitted work, or discards a batch that was never submitted.
        void discardBatch() noexcept;
        [[nodiscard]] VkDeviceSize stagedByteCount() const noexcept { return stagedBytes_; }

        /// Uploads CPU data into a new device-local destination buffer.
        [[nodiscard]] Buffer uploadBuffer(
            const void *data,
            VkDeviceSize size,
            VkBufferUsageFlags destinationUsage);

        // Recording only: an explicit batch must be open. Caller owns rollback.
        static void validateBufferUpload(const BufferUploadInfo& info);
        static void validateImageUploadInfo(const ImageUploadInfo& info);
        void recordBufferUpload(const BufferUploadInfo& info);
        void recordImageUpload(const ImageUploadInfo& info);

        /// Uploads one or more buffer regions into an existing image.
        void uploadImage(const ImageUploadInfo& uploadInfo);

    private:
        /// 借用VKDevice
        const Device *device_ = nullptr;
        /// 借用cmdPool
        CommandPool *commandPool_ = nullptr;
        VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;
        VkFence fence_ = VK_NULL_HANDLE;
        bool submitted_ = false;
        std::vector<Buffer> stagingBuffers_;
        VkDeviceSize stagedBytes_ = 0;
    };

} // namespace rubia::rhi::vulkan
