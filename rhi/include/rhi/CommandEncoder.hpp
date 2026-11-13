#pragma once
#include "rhi/RenderEncoder.hpp"
#include <array>

namespace rubia::rhi
{
enum class LoadOp { Discard, Load, Clear };
enum class StoreOp { Discard, Store };
struct AttachmentDesc
{
    Format format = Format::Undefined;
    uint32_t samples = 1;
    LoadOp load = LoadOp::Clear, stencilLoad = LoadOp::Discard;
    StoreOp store = StoreOp::Store, stencilStore = StoreOp::Discard;
    ResourceAccess before, after;
};
struct ColorAttachmentUse
{
    uint32_t attachment = 0;
    int32_t resolve = -1;
};
/// Single graphics pass. Attachment indices refer to the framebuffer view order.
struct RenderPassLayoutDesc
{
    std::vector<AttachmentDesc> attachments;
    std::vector<ColorAttachmentUse> colors;
    int32_t depthStencil = -1;
};
struct ClearValue
{
    std::array<float, 4> color{0, 0, 0, 1};
    float depth = 1;
    uint32_t stencil = 0;
    bool depthStencil = false;
};

/// Borrowed, prepared framebuffer view. The host retains its attachments and
/// native framebuffer until submission completion. Never store beyond that use.
class PassTarget
{
public:
    virtual ~PassTarget() = default;
    virtual DeviceId deviceId() const noexcept = 0;
    virtual uint32_t width() const noexcept = 0;
    virtual uint32_t height() const noexcept = 0;
};
struct RenderPassDesc
{
    const PassTarget* target = nullptr;
    std::vector<ClearValue> clears;
};
class CommandEncoder
{
public:
    virtual ~CommandEncoder() = default;
    virtual RenderEncoder& beginPass(const RenderPassDesc&) = 0;
    virtual void endPass() = 0;
    // Explicit transition outside a pass. Does not mutate authoritative resource
    // state; the owner commits state only after successful queue submission.
    virtual void transition(const TextureRef&, const SubresourceRange&,
                            ResourceAccess before, ResourceAccess after) = 0;
};
} // namespace rubia::rhi
