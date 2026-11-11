#pragma once

namespace rubia::render
{

/// Typed extension point for a backend's active UI render pass.
/// Kept separate from native API types so Engine only sees a typed extension point.
class GuiCommandContext
{
public:
    virtual ~GuiCommandContext() = default;
};

/// Borrowed UI commands, valid until the GUI begins its next frame.
class GuiOverlay
{
public:
    virtual ~GuiOverlay() = default;

    /// Records into an existing pass. Must not submit, end the pass, or change
    /// attachment layouts. The caller owns frame reuse and submission safety.
    virtual void record(GuiCommandContext& context) const = 0;
};

} // namespace rubia::render
