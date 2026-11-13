#pragma once
#include "render/GuiOverlay.hpp"
#include "rhi/Resources.hpp"
#include <cstdint>
#include <memory>

struct ImGuiContext;
struct ImDrawData;

namespace rubia::integration::imgui
{
// Owns a GUI registration and retains its texture view. Token is borrowed and
// only valid while this registration and its backend session are alive.
// Retire the owner through the render scheduler after the last submitted use.
class GuiTexture
{
public:
    virtual ~GuiTexture() = default;
    virtual std::uintptr_t token() const noexcept = 0;
};
using GuiTextureRef = std::shared_ptr<const GuiTexture>;

class GuiRendererBackend : public render::GuiOverlay
{
public:
    virtual void initialize(ImGuiContext*) = 0;
    // GPU work and external texture registrations must be drained first.
    virtual void shutdown() noexcept = 0;
    virtual bool ready() const noexcept = 0;
    virtual void newFrame() = 0;
    // Font/texture uploads happen here, outside the active graphics pass.
    virtual void prepare(ImDrawData*) = 0;
    // Refresh from the backend's output source after presentation reconfiguration.
    // Requires idle and drained external registrations; may rebuild the session.
    virtual void reconfigure() = 0;
    virtual GuiTextureRef registerTexture(const rhi::TextureViewRef&) = 0;
};

class GuiPlatformBackend
{
public:
    virtual ~GuiPlatformBackend() = default;
    virtual void initialize(ImGuiContext*) = 0;
    virtual void shutdown() noexcept = 0;
    virtual void newFrame() = 0;
    virtual float contentScale() const = 0;
};
}
