#pragma once
#include "render/ResourcePreparationTypes.hpp"
#include <cstddef>

namespace rubia::render
{
// A pipeline request is a pass-specific set of variants, not an asset upload.
struct PipelinePreparationTicket
{
    uint64_t value = 0;
    explicit operator bool() const noexcept { return value != 0; }
};
enum class PipelinePreparationState { Queued, Preparing, Ready, Failed, Cancelled };
inline bool pipelinePreparationFinished(PipelinePreparationState state) noexcept
{
    return state == PipelinePreparationState::Ready || state == PipelinePreparationState::Failed ||
           state == PipelinePreparationState::Cancelled;
}
struct PipelinePreparationStatus
{
    PipelinePreparationState state = PipelinePreparationState::Queued;
    std::size_t total = 0; // Unique full pipeline keys, not material count.
    std::size_t completed = 0;
    std::string error;
};
struct PipelinePreparationResult
{
    ResourcePreparationCode code = ResourcePreparationCode::InvalidRequest;
    PipelinePreparationTicket ticket;
    std::string error;
    ResourcePreparationDisposition disposition = ResourcePreparationDisposition::Started;
    bool accepted() const noexcept { return code == ResourcePreparationCode::Accepted; }
};
} // namespace rubia::render
