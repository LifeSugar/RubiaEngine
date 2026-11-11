#pragma once

#include "resources/RetirementBatch.hpp"
#include "rhi/Completion.hpp"
#include <cstddef>
#include <vector>

namespace rubia::render
{

/// Render-thread ownership ledger for one ordered graphics queue. The host
/// performs all native waits, command resets, acquisition and submission.
/// Its device/session must outlive this ledger and its retained resources.
class FrameScheduler final
{
public:
    explicit FrameScheduler(std::size_t slotCount);
    FrameScheduler(const FrameScheduler&) = delete;
    FrameScheduler& operator=(const FrameScheduler&) = delete;

    /// Call only after this slot's native completion wait AND command reset
    /// succeed. An acquisition/recording failure can leave this slot reusable;
    /// calling begin again is safe and does not consume pending retirement.
    void beginReusableSlot(std::size_t slot);

    /// Obtain before native submission, while sequence exhaustion can throw.
    /// Failure/skip simply discards this candidate; it does not advance time.
    [[nodiscard]] rhi::CompletionPoint candidate() const;

    /// Publish immediately after native submit succeeds, even when presenting
    /// later fails. Requires a begun, unsubmitted slot and the latest candidate.
    /// Preallocated slots and an empty reusable batch make this no-throw and
    /// ensure no resource destruction occurs after successful native submit.
    void commitSuccess(std::size_t slot, rhi::CompletionPoint point) noexcept;

    /// Host must first establish graphics-queue/device idle and reset every
    /// command buffer retaining these resources. This also drains pending
    /// owners that never reached a successful submission.
    void collectAfterIdle() noexcept;

    [[nodiscard]] RetirementBatch& pending() noexcept { return pending_; }
    [[nodiscard]] const RetirementBatch& pending() const noexcept { return pending_; }
    [[nodiscard]] bool isComplete(rhi::CompletionPoint point) const;
    [[nodiscard]] rhi::CompletionPoint lastSubmitted() const noexcept;
    [[nodiscard]] rhi::CompletionPoint completion(std::size_t slot) const;
    [[nodiscard]] std::size_t slotCount() const noexcept { return slots_.size(); }

private:
    struct Slot
    {
        RetirementBatch retired;
        rhi::CompletionPoint completion;
        bool reusable = false;
    };

    rhi::SubmissionTimeline timeline_;
    std::vector<Slot> slots_;
    RetirementBatch pending_;
};

} // namespace rubia::render
