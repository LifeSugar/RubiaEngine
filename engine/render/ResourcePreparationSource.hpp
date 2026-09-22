#pragma once
#include "asset/AssetSnapshot.hpp"
#include <stdexcept>
#include <type_traits>

namespace rubia::render
{
// Resources with a rendering representation. Model remains a CPU scene composition.
using ResourceAssetHandle =
    std::variant<asset::TextureAssetHandle, asset::MeshAssetHandle, asset::ShaderAssetHandle,
                 asset::ShaderProgramAssetHandle, asset::MaterialTemplateAssetHandle,
                 asset::MaterialAssetHandle>;
using ResourceAssetSnapshot = std::variant<
    asset::AssetSnapshot<asset::TextureAsset>, asset::AssetSnapshot<asset::MeshAsset>,
    asset::AssetSnapshot<asset::ShaderAsset>, asset::AssetSnapshot<asset::ShaderProgramAsset>,
    asset::AssetSnapshot<asset::MaterialTemplateAsset>, asset::AssetSnapshot<asset::MaterialAsset>>;

inline ResourceAssetHandle resourceHandle(const asset::AnyAssetHandle& handle)
{
    return std::visit(
        [](auto value) -> ResourceAssetHandle
        {
            if constexpr (std::is_constructible_v<ResourceAssetHandle, decltype(value)>)
            {
                return value;
            }
            else
            {
                throw std::invalid_argument("CPU composition has no render resource handle");
            }
        },
        handle);
}
inline ResourceAssetSnapshot resourceSnapshot(const asset::AnyAssetSnapshot& source)
{
    return std::visit(
        [](const auto& value) -> ResourceAssetSnapshot
        {
            if constexpr (std::is_constructible_v<ResourceAssetSnapshot, decltype(value)>)
            {
                return value;
            }
            else
            {
                throw std::invalid_argument(
                    "CPU composition must be expanded before GPU preparation");
            }
        },
        source);
}
} // namespace rubia::render
