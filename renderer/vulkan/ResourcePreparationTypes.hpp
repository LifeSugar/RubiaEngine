#pragma once
#include "render/ResourcePreparationTypes.hpp"

namespace rubia::rhi::vulkan
{
// Backend and frontend share one ticket/status contract; no second task registry.
using render::ResourcePreparationCode;
using render::ResourcePreparationDisposition;
using render::resourcePreparationFinished;
using render::ResourcePreparationResult;
using render::ResourcePreparationState;
using render::ResourcePreparationStatus;
using render::ResourcePreparationTicket;
} // namespace rubia::rhi::vulkan
