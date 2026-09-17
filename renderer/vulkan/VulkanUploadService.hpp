#pragma once

#include "vulkan/UploadContext.hpp"
#include "vulkan/VulkanUploadTypes.hpp"

#include <deque>
#include <map>
#include <thread>

namespace rubia::rhi::vulkan
{
// Render-thread-only. Device outlives service. Sources/targets may not be
// mutated, reset or moved while retained. Image updates require caller-managed states
// and exclusive access until GPU completion; Cancelled alone does not imply completion.
// All work uses the graphics queue; no cross-queue ownership transfer or state tracking.
// 保存上传需求，根据预算选择本帧处理的操作；组织并提交GPU命令，查询上传操作情况；保留上传期间不能释放的数据和资源
class VulkanUploadService final
{
public:
    explicit VulkanUploadService(const Device& device, UploadLimits limits = {});
    ~VulkanUploadService();
    VulkanUploadService(const VulkanUploadService&) = delete;
    VulkanUploadService& operator=(const VulkanUploadService&) = delete;

    // Consumes only on Accepted. Rejection leaves request intact.
    UploadEnqueueResult tryEnqueue(UploadRequest& request);
    UploadStatus query(UploadTicket ticket) const;
    void cancel(UploadTicket ticket);
    void releaseTicket(UploadTicket ticket); // Terminal tickets only.
    void tick(const UploadBudget& budget = {});
    void drain(); // Completes accepted work; explicit blocking path.
    // Shutdown cancels queued work and drains submitted commands before releasing references.
    void shutdown() noexcept;
    uint64_t stagedBytes() const noexcept
    {
        return uploadsContext_.stagedByteCount();
    }

private:
    struct Record
    {
        UploadRequest request;
        UploadStatus status;
        std::size_t next = 0; //下一个录制的操作（op）下标
        std::size_t completed = 0; //已经确认完成的数量
        bool accounted = true; //这个请求的数据量是否还计入容量占用
    };
    struct Part
    {
        std::shared_ptr<Record> record;  //属于哪个record 请求
        UploadOperation operation; //具体的上传操作
        uint64_t bytes; //本次操作的大小
    };
    void checkThread() const;
    void retire(const std::shared_ptr<Record>& record);
    void finishBatch();
    void failService(const std::string& error);

    const Device& device_;
    UploadLimits limits_;
    std::thread::id thread_;
    CommandPool commandPool_;
    UploadContext uploadsContext_;
    std::map<uint64_t, std::shared_ptr<Record>> records_;
    std::deque<uint64_t> queue_;
    std::vector<Part> batch_;
    uint64_t nextTicket_ = 1;
    uint64_t queuedBytes_ = 0;
    std::string serviceError_;
    bool stopped_ = false;
};
} // namespace rubia::rhi::vulkan
