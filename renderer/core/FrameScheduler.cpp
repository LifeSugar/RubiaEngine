#include "FrameScheduler.hpp"
#include <cassert>
#include <stdexcept>

namespace rubia::render
{

FrameScheduler::FrameScheduler(std::size_t slotCount) : slots_(slotCount)
{
    if (slotCount == 0)
        throw std::invalid_argument("frame scheduler requires at least one slot");
}

void FrameScheduler::beginReusableSlot(std::size_t slot)
{
    auto& state = slots_.at(slot);
    timeline_.completed(state.completion);
    state.retired.clear();
    state.reusable = true;
}

rhi::CompletionPoint FrameScheduler::candidate() const
{
    return timeline_.candidate();
}

void FrameScheduler::commitSuccess(std::size_t slot, rhi::CompletionPoint point) noexcept
{
    assert(slot < slots_.size());
    auto& state = slots_[slot];
    assert(state.reusable && state.retired.empty());
    timeline_.submitted(point);
    state.completion = point;
    state.retired.swap(pending_);
    state.reusable = false;
}

void FrameScheduler::collectAfterIdle() noexcept
{
    timeline_.completeAfterIdle();
    for (auto& state : slots_)
    {
        state.retired.clear();
        state.reusable = false;
    }
    pending_.clear();
}

bool FrameScheduler::isComplete(rhi::CompletionPoint point) const
{
    return timeline_.isComplete(point);
}

rhi::CompletionPoint FrameScheduler::lastSubmitted() const noexcept
{
    return timeline_.lastSubmitted();
}

rhi::CompletionPoint FrameScheduler::completion(std::size_t slot) const
{
    return slots_.at(slot).completion;
}

} // namespace rubia::render
