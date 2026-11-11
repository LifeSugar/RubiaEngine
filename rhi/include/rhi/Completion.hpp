#pragma once

#include <atomic>
#include <cassert>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace rubia::rhi
{

/// One graphics-queue submission in a particular device/session lifetime.
/// A reusable native fence is deliberately not part of this identity.
struct CompletionPoint
{
    uint64_t session = 0;
    uint64_t serial = 0;
    explicit operator bool() const noexcept { return session != 0 && serial != 0; }
};

/// Render-thread ledger. The backend reports native fence completion; this
/// class never submits, waits, or assumes that presentation has completed.
class SubmissionTimeline final
{
public:
    SubmissionTimeline() : session_(allocateSession()) {}
    SubmissionTimeline(const SubmissionTimeline&) = delete;
    SubmissionTimeline& operator=(const SubmissionTimeline&) = delete;

    /// Check before native submission, so publishing success cannot throw.
    CompletionPoint candidate() const
    {
        if (submitted_ == std::numeric_limits<uint64_t>::max())
            throw std::overflow_error("graphics submission sequence exhausted");
        return {session_, submitted_ + 1};
    }
    void submitted(CompletionPoint point) noexcept
    {
        assert(point.session == session_ && point.serial == submitted_ + 1);
        submitted_ = point.serial;
    }
    /// Called only after the protecting native fence/queue has completed.
    void completed(CompletionPoint point) noexcept
    {
        if (!point) return;
        assert(point.session == session_ && point.serial <= submitted_);
        if (point.serial > completed_) completed_ = point.serial;
    }
    bool isComplete(CompletionPoint point) const
    {
        if (!point) return true;
        if (point.session != session_ || point.serial > submitted_)
            throw std::invalid_argument("completion point is not a submitted point in this session");
        return point.serial <= completed_;
    }
    CompletionPoint lastSubmitted() const noexcept
    {
        return submitted_ ? CompletionPoint{session_, submitted_} : CompletionPoint{};
    }
    void completeAfterIdle() noexcept { completed_ = submitted_; }

private:
    static uint64_t allocateSession()
    {
        static std::atomic<uint64_t> next{1};
        auto value = next.load(std::memory_order_relaxed);
        do
        {
            if (value == std::numeric_limits<uint64_t>::max())
                throw std::overflow_error("graphics session identity exhausted");
        } while (!next.compare_exchange_weak(value, value + 1, std::memory_order_relaxed));
        return value;
    }
    uint64_t session_;
    uint64_t submitted_ = 0;
    uint64_t completed_ = 0;
};
} // namespace rubia::rhi
