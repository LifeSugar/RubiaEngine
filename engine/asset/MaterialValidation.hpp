#pragma once

#include "asset/MaterialAsset.hpp"
#include "asset/MaterialTemplateAsset.hpp"
#include "asset/ValidationReport.hpp"

#include <cstdint>
#include <vector>

namespace rubia::asset
{

class AssetManager;

[[nodiscard]] ValidationReport validateMaterialTemplateCreateInfo(
    const MaterialTemplateAsset::CreateInfo& createInfo,
    const AssetManager& assets);

[[nodiscard]] ValidationReport validateMaterialCreateInfo(
    const MaterialAsset::CreateInfo& createInfo,
    const AssetManager& assets);

} // namespace rubia::asset
