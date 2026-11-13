#pragma once
#include "resources/RetirementBatch.hpp"
namespace rubia::rhi::vulkan
{
// Compatibility name while renderer services migrate to the common owner batch.
using RetiredResources = render::RetirementBatch;
}
