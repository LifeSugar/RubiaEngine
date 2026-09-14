#pragma once

#include "vulkan/Buffer.hpp"
#include "vulkan/CommandPool.hpp"
#include "vulkan/Device.hpp"
#include "vulkan/Image.hpp"

#include <cstddef>
#include <cstdint>
#include <chrono>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace rubia::rhi::vulkan
{
    //描述cpu端数据
    struct UploadBytes
    {
        // 保证 data 指向的数据在上传准备期间有效、地址稳定。
        std::shared_ptr<const void> owner;

        const std::byte *data = nullptr;
        std::size_t size = 0;
    };


    //描述“把数据写入哪个buffer”，只描述传输
    struct BufferUpload
    {
        std::shared_ptr<const Buffer> destination;
        VkDeviceSize destinationOffset = 0;

        UploadBytes source;

        VkPipelineStageFlags finalStages = 0;
        VkAccessFlags finalAccess = 0;
    };

    //描述“把数据写入哪张GPU Texture”，只描述传输
    struct ImageUpload
    {
        std::shared_ptr<const Image> destination;

        UploadBytes source;

        std::vector<VkBufferImageCopy> regions;
        VkImageSubresourceRange range{};

        VkImageLayout finalLayout =
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkPipelineStageFlags finalStages = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        VkAccessFlags finalAccess = VK_ACCESS_SHADER_READ_BIT;
    };

    //这里统一表示一种上传操作
    using UploadOperation = std::variant<BufferUpload, ImageUpload>;
    //一组可追踪的上传请求
    struct UploadRequest
    {
        std::vector<UploadOperation> operations;
    };

    //请求的编号，用于查询
    struct UploadTicket
    {
        uint64_t value = 0;
        explicit operator bool() const noexcept { return value != 0; }
    };

    enum class UploadState
    {
        Queued, //接收请求 排队中 没提交
        Uploading,  //已提交gpu，但是尚未确认完成
        Completed, //确认GPU已经复制完整数据
        Failed, //错了
        Cancelled //不要了
    };

    inline bool uploadFinished(UploadState state) noexcept
    {
        return state == UploadState::Completed || state == UploadState::Failed ||
               state == UploadState::Cancelled;
    }

    //接收之后，请求执行到哪了，但是进度不能逐字节查询
    struct UploadStatus
    {
        UploadState state = UploadState::Queued;
        uint64_t totalBytes = 0; //需要提交多少
        uint64_t submittedBytes = 0;//已经向GPU提交的进度
        uint64_t completedBytes = 0;//gpu 确认完成的进度
        std::string error;
    };

    
    enum class UploadEnqueueCode
    {
        Accepted, //service接收请求
        QueueFull,//队列容量不足，保留请求，稍后重试
        InvalidRequest,//请求不符合要求
        ServiceFailed//service 挂了
    };
    //提交请求后接收的状态，service 接收请返回 uploads_->tryEnqueue(request);
    struct UploadEnqueueResult
    {
        UploadEnqueueCode code = UploadEnqueueCode::InvalidRequest;
        UploadTicket ticket;
        std::string error;
        bool accepted() const noexcept { return code == UploadEnqueueCode::Accepted; }
    };

    //本此tick()的batch处理多少工作 默认16mb 16个操作，4ms。软预算 一个不拆分的op可能超过
    struct UploadBudget
    {
        // Soft limits: one indivisible operation may exceed the byte/time budget.
        uint64_t maxBytesPerTick = 16ull * 1024 * 1024;
        std::size_t maxOperationsPerTick = 16;
        std::chrono::microseconds maxCpuTime{4000};
    };

    //service允许的总容量
    struct UploadLimits
    {
        // 一个批次的 staging 字节上限；单个操作也不能超过它
        uint64_t maxStagingBytes = 256ull * 1024 * 1024;
        // 活跃请求累计的逻辑源数据字节上限
        uint64_t maxQueuedBytes = 512ull * 1024 * 1024;
        //尚未释放的请求记录数量上限
        std::size_t maxRequests = 1024;
    };

}
