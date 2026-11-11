#pragma once
#include "rhi/Resources.hpp"
#include <chrono>
namespace rubia::rhi
{
struct BufferUpload
{
    BufferRef destination;
    uint64_t destinationOffset = 0;
    SourceBytes source;
    ResourceAccess after;
};
struct TextureUpload
{
    TextureRef destination;
    SourceBytes source;
    std::vector<TextureCopyRegion> regions;
    SubresourceRange range;
    ResourceAccess before, after;
};
using UploadOperation = std::variant<BufferUpload, TextureUpload>;
struct UploadRequest
{
    std::vector<UploadOperation> operations;
};
struct UploadTicket
{
    uint64_t value = 0;
    explicit operator bool() const noexcept { return value != 0; }
};
enum class UploadState
{
    Queued,
    Uploading,
    Completed,
    Failed,
    Cancelled
};
inline bool uploadFinished(UploadState state) noexcept
{
    return state == UploadState::Completed || state == UploadState::Failed ||
           state == UploadState::Cancelled;
}
struct UploadStatus
{
    UploadState state = UploadState::Queued;
    uint64_t totalBytes = 0, submittedBytes = 0, completedBytes = 0;
    std::string error;
};
enum class UploadEnqueueCode
{
    Accepted,
    QueueFull,
    InvalidRequest,
    UnsupportedRequest,
    ServiceFailed
};
struct UploadEnqueueResult
{
    UploadEnqueueCode code = UploadEnqueueCode::InvalidRequest;
    UploadTicket ticket;
    std::string error;
    bool accepted() const noexcept { return code == UploadEnqueueCode::Accepted; }
};
struct UploadBudget
{
    uint64_t maxBytesPerTick = 16ull * 1024 * 1024;
    std::size_t maxOperationsPerTick = 16;
    std::chrono::microseconds maxCpuTime{4000};
};
struct UploadLimits
{
    uint64_t maxStagingBytes = 256ull * 1024 * 1024, maxQueuedBytes = 512ull * 1024 * 1024;
    std::size_t maxRequests = 1024;
};
class UploadService
{
  public:
    virtual ~UploadService() = default;
    // Rejection does not consume request. Cancellation does not end in-flight leases.
    virtual UploadEnqueueResult tryEnqueue(UploadRequest &) = 0;
    virtual UploadStatus query(UploadTicket) const = 0;
    virtual void cancel(UploadTicket) = 0;
    virtual void releaseTicket(UploadTicket) = 0;
    virtual void tick(const UploadBudget &budget = {}) = 0;
    virtual void drain() = 0;
    virtual void shutdown() noexcept = 0;
    virtual uint64_t stagedBytes() const noexcept = 0;
};
} // namespace rubia::rhi
