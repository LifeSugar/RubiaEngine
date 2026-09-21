#pragma once

#include "render/PipelineVariantKey.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace rubia::rhi::vulkan
{

class GraphicsPipeline;
class GpuMaterial;
class Mesh;

/// One backend-local draw with Asset handles resolved to Vulkan resources.
struct VulkanDrawItem
{
    const Mesh* mesh = nullptr;
    const GpuMaterial* material = nullptr;
    render::PipelineVariantKey pipelineKey;
    std::shared_ptr<const GraphicsPipeline> pipeline;
    uint32_t submeshIndex = 0;
    uint32_t objectIndex = 0;
};

/// Pipeline owners are retained; mesh/material pointers borrow the resident cache.
/// Consume before the next resource publication, and retain GPU owners until completion.
/// Transient Vulkan resource resolution of one backend-neutral RenderList.
struct VulkanDrawList
{
    std::vector<VulkanDrawItem> opaque;
    std::vector<VulkanDrawItem> transparent;

    [[nodiscard]] std::size_t size() const noexcept
    {
        return opaque.size() + transparent.size();
    }
};

} // namespace rubia::rhi::vulkan
