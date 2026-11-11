#pragma once
#include "rhi/CommandEncoder.hpp"
#include "vulkan/RenderPass.hpp"
#include "vulkan/Device.hpp"
namespace rubia::rhi::vulkan
{
RenderPass createRenderPass(const Device&, const rhi::RenderPassLayoutDesc&);
}
