#include "core/PresentationFrame.hpp"
#include <stdexcept>

namespace rubia::render
{
PresentationFrameResult executePresentationFrame(rhi::PresentationSurface& surface,
    rhi::FrameExecutor& executor, FrameScheduler& scheduler, uint32_t slot,
    const RecordPresentationFrame& record)
{
    if (executor.slotCount() != scheduler.slotCount() || slot >= executor.slotCount())
        throw std::invalid_argument("presentation executor/scheduler slot mismatch");
    executor.prepareSlot(slot);
    scheduler.beginReusableSlot(slot);
    auto acquired = surface.acquire(slot);
    if (!acquired.frame)
    {
        if (acquired.status == rhi::PresentationStatus::Ready ||
            acquired.status == rhi::PresentationStatus::Suboptimal)
            throw std::logic_error("presentation backend returned success without a frame");
        return {false, acquired.status};
    }
    if (acquired.status != rhi::PresentationStatus::Ready &&
        acquired.status != rhi::PresentationStatus::Suboptimal)
        throw std::logic_error("presentation backend returned an invalid frame status");
    auto& commands = executor.beginCommands(*acquired.frame);
    record(*acquired.frame, commands);
    executor.endCommands(*acquired.frame);
    const auto point = scheduler.candidate();
    executor.submit(*acquired.frame);
    scheduler.commitSuccess(slot, point);
    auto status = executor.present(*acquired.frame);
    if (status == rhi::PresentationStatus::Ready && acquired.status == rhi::PresentationStatus::Suboptimal)
        status = rhi::PresentationStatus::Suboptimal;
    return {true, status};
}
}
