#pragma once

#include "asset/MaterialTemplateAsset.hpp"
#include "asset/ValidationReport.hpp"

namespace rubia::asset
{
class AssetManager;

/// Extracts material-set resources from a validated program. The initial
/// material backend supports one UBO and scalar separate image/sampler slots.
class MaterialTemplateBuilder final
{
  public:
    [[nodiscard]] static MaterialTemplateAsset::CompiledCreateInfo build(
        const MaterialTemplateAsset::CreateInfo &createInfo, const AssetManager &assets,
        ValidationReport &report);
};
} // namespace rubia::asset
