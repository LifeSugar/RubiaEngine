#pragma once
#include "resources/MeshGeometry.hpp"
#include "resources/MaterialBinding.hpp"
#include "render/PipelineVariantKey.hpp"
#include "rhi/GraphicsPipeline.hpp"

namespace rubia::render
{
struct PreparedDrawItem
{
    const MeshGeometry* mesh = nullptr;
    const MaterialBinding* material = nullptr;
    PipelineVariantKey pipelineKey;
    rhi::PipelineRef pipeline;
    uint32_t submeshIndex = 0, objectIndex = 0;
};
/// Mesh/material references borrow one frozen publication interval. Pipelines
/// retain their exact immutable owners. Consume before the next publication.
struct PreparedDrawList
{
    std::vector<PreparedDrawItem> opaque, transparent;
    std::size_t size() const noexcept { return opaque.size() + transparent.size(); }
};
} // namespace rubia::render
