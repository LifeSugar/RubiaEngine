#pragma once
#include "rhi/GraphicsPipeline.hpp"

namespace rubia::rhi
{
enum class IndexType { UInt16, UInt32 };

/// Commands inside an already active graphics pass. Resource ownership remains
/// with the caller until its submission completes. No Asset/Material semantics.
class RenderEncoder
{
public:
    virtual ~RenderEncoder() = default;
    virtual void bindPipeline(const PipelineRef&) = 0;
    virtual void bindResources(uint32_t group, const BindingSetRef&) = 0;
    virtual void bindVertexBuffer(uint32_t binding, const BufferRef&, uint64_t offset = 0) = 0;
    virtual void bindIndexBuffer(const BufferRef&, IndexType, uint64_t offset = 0) = 0;
    virtual void setViewport(const Viewport&) = 0;
    virtual void setScissor(const Rect&) = 0;
    virtual void setConstants(ShaderStageMask, uint32_t offset, uint32_t size, const void*) = 0;
    virtual void draw(uint32_t vertices, uint32_t instances, uint32_t firstVertex, uint32_t firstInstance) = 0;
    virtual void drawIndexed(uint32_t indices, uint32_t instances, uint32_t firstIndex,
                             int32_t vertexOffset, uint32_t firstInstance) = 0;
};
} // namespace rubia::rhi
