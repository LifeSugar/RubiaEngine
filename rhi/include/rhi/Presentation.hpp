#pragma once
#include "rhi/CommandEncoder.hpp"
#include <memory>

namespace rubia::rhi
{
enum class OutputColorSpace { SrgbNonlinear, Unknown };
enum class PresentMode { Fifo, Mailbox, Immediate };
struct PresentationConfig
{
    uint32_t width = 0, height = 0;
    // SDR surface preferences; inspect output() for the negotiated configuration.
    // UNorm preserves the current GUI's display-encoded vertex colors.
    Format preferredFormat = Format::BGRA8UNorm;
    PresentMode preferredPresentMode = PresentMode::Mailbox;
};
struct OutputDescription
{
    uint32_t width = 0, height = 0;
    Format format = Format::Undefined;
    OutputColorSpace colorSpace = OutputColorSpace::Unknown;
    PresentMode presentMode = PresentMode::Fifo;
};
enum class PresentationStatus
{
    Ready, Suboptimal, Unavailable, NeedsReconfigure, SurfaceLost
};

// A short-lived lease, not a persistent texture or an image index. A default
// framebuffer can implement PassTarget without exposing any texture resource.
// Surface/executor must outlive leases. Destroy a lease before reconfiguration.
// Abandoning a frame delegates synchronization recovery to its backend.
class AcquiredFrame
{
public:
    AcquiredFrame() = default;
    virtual ~AcquiredFrame() = default;
    AcquiredFrame(const AcquiredFrame&) = delete;
    AcquiredFrame& operator=(const AcquiredFrame&) = delete;
    virtual const PassTarget& target() const noexcept = 0;
    virtual const OutputDescription& output() const noexcept = 0;
};
struct AcquireResult
{
    PresentationStatus status = PresentationStatus::Unavailable;
    std::unique_ptr<AcquiredFrame> frame;
};
class PresentationSurface
{
public:
    virtual ~PresentationSurface() = default;
    // Caller first establishes device idle and discards recorded uses. Zero
    // extent suspends acquisition; a later nonzero configure resumes it.
    virtual void configure(const PresentationConfig&) = 0;
    virtual OutputDescription output() const noexcept = 0;
    // frameSlot selects CPU resources, never a presentation image. Ready and
    // Suboptimal must carry a frame; all other statuses must carry none.
    virtual AcquireResult acquire(uint32_t frameSlot) = 0;
};
class FrameExecutor
{
public:
    virtual ~FrameExecutor() = default;
    virtual uint32_t slotCount() const noexcept = 0;
    // Wait for GPU completion AND discard recorded uses before returning.
    virtual void prepareSlot(uint32_t frameSlot) = 0;
    virtual CommandEncoder& beginCommands(AcquiredFrame&) = 0;
    virtual void endCommands(AcquiredFrame&) = 0;
    // Success means GPU submission occurred. Must not throw after success.
    // The caller commits its lifetime ledger immediately, before present().
    virtual void submit(AcquiredFrame&) = 0;
    // Consumes the submitted frame for presentation. A failed presentation does
    // not undo submission. SurfaceLost requires rebuilding the platform surface.
    virtual PresentationStatus present(AcquiredFrame&) = 0;
    // Caller establishes device idle before discarding recorded uses.
    virtual void resetCommandsAfterIdle() = 0;
};
}
