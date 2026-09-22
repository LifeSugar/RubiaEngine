#pragma once
#include <cstdint>
#include <string>

namespace rubia::render
{
// Separate from UploadTicket: a preparation can require zero upload requests.
struct ResourcePreparationTicket
{
    uint64_t value = 0;
    explicit operator bool() const noexcept
    {
        return value != 0;
    }
};
enum class ResourcePreparationState
{
    Preparing, // Waiting for dependencies, worker creation, transfer admission, or publication.
    Uploading,
    Ready, // This snapshot and its dependencies are resident; not a pipeline rebuild.
    Failed,
    Cancelled
};
inline bool resourcePreparationFinished(ResourcePreparationState state) noexcept
{
    return state == ResourcePreparationState::Ready || state == ResourcePreparationState::Failed ||
           state == ResourcePreparationState::Cancelled;
}
struct ResourcePreparationStatus
{
    ResourcePreparationState state = ResourcePreparationState::Preparing;
    // Own transfer only; shared dependency bytes are not aggregated.
    uint64_t totalBytes = 0;
    uint64_t submittedBytes = 0;
    uint64_t completedBytes = 0;
    std::string error;
};
enum class ResourcePreparationCode
{
    Accepted,
    QueueFull,
    InvalidRequest,
    UnsupportedRequest,
    Failed
};

// All accepted requests get a distinct caller ticket, even when they share work or hit cache.
enum class ResourcePreparationDisposition
{
    Started,
    Shared,
    CacheHit
};

struct ResourcePreparationResult
{
    ResourcePreparationCode code = ResourcePreparationCode::InvalidRequest;
    ResourcePreparationTicket ticket;
    std::string error;
    ResourcePreparationDisposition disposition = ResourcePreparationDisposition::Started;
    bool accepted() const noexcept
    {
        return code == ResourcePreparationCode::Accepted;
    }
};
} // namespace rubia::render
