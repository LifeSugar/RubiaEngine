#include "render/ResourcePreparation.hpp"
#include "asset/AssetManager.hpp"
#include <optional>

namespace rubia::render
{
ResourcePreparationResult ResourcePreparation::prepare(const asset::AssetManager& assets,
                                                       ResourceAssetHandle handle)
{
    std::optional<ResourceAssetSnapshot> source;
    try
    {
        source = std::visit([&](auto value) -> ResourceAssetSnapshot
                            { return assets.snapshot(value); }, handle);
    }
    catch (const std::exception& error)
    {
        return {ResourcePreparationCode::InvalidRequest, {}, error.what()};
    }
    return prepare(std::move(*source));
}
} // namespace rubia::render
