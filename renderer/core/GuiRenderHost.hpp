#pragma once
#include "core/TextureOutput.hpp"
#include <memory>

namespace rubia::render
{
// Scene outputs and resource retirement used by GUI integration, independent of
// a graphics backend or of Dear ImGui itself.
class GuiRenderHost
{
public:
    virtual ~GuiRenderHost() = default;
    virtual bool hasViewport() const noexcept = 0;
    virtual TextureOutput::Extent outputExtent() const noexcept = 0;
    virtual uint32_t currentFrameIndex() const noexcept = 0;
    virtual uint32_t frameCount() const noexcept = 0;
    virtual TextureOutput viewportOutput(uint32_t slot) const = 0;
    virtual void resizeViewport(uint32_t width, uint32_t height) = 0;
    virtual void waitIdle() const = 0;
    virtual void retireGuiResource(std::shared_ptr<const void>) = 0;
    virtual void drainGuiResources() = 0;
};
}
