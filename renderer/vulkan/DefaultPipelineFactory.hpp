#pragma once

#include "vulkan/GraphicsPipeline.hpp"
#include "vulkan/GpuPreparedAssets.hpp"

namespace rubia::rhi::vulkan
{

[[nodiscard]] GraphicsPipeline::CreateInfo makeDefaultScenePipeline(
    std::shared_ptr<const GpuShaderProgram> program,
    rhi::BindingLayoutRef materialDescriptorSetLayout);

[[nodiscard]] GraphicsPipeline::CreateInfo makeDefaultPresentPipeline(
    std::shared_ptr<const GpuShaderProgram> program);

} // namespace rubia::rhi::vulkan
