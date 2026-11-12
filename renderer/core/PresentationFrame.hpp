#pragma once
#include "core/FrameScheduler.hpp"
#include "rhi/Presentation.hpp"
#include <functional>

namespace rubia::render
{
struct PresentationFrameResult
{
    bool submitted = false;
    rhi::PresentationStatus status = rhi::PresentationStatus::Unavailable;
};
using RecordPresentationFrame = std::function<void(rhi::AcquiredFrame&, rhi::CommandEncoder&)>;
// One graphics submission per acquired frame. Slot rotation belongs to caller.
PresentationFrameResult executePresentationFrame(rhi::PresentationSurface&, rhi::FrameExecutor&,
    FrameScheduler&, uint32_t slot, const RecordPresentationFrame&);
}
