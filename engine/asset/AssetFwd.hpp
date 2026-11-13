#pragma once

#include "asset/AssetHandle.hpp"
#include <variant>

namespace rubia::asset
{

class MaterialAsset;
class MaterialTemplateAsset;
class MeshAsset;
class ModelAsset;
class ShaderAsset;
class ShaderProgramAsset;
class TextureAsset;

using MaterialAssetHandle = AssetHandle<MaterialAsset>;
using MaterialTemplateAssetHandle = AssetHandle<MaterialTemplateAsset>;
using MeshAssetHandle = AssetHandle<MeshAsset>;
using ModelAssetHandle = AssetHandle<ModelAsset>;
using ShaderAssetHandle = AssetHandle<ShaderAsset>;
using ShaderProgramAssetHandle = AssetHandle<ShaderProgramAsset>;
using TextureAssetHandle = AssetHandle<TextureAsset>;

using AnyAssetHandle =
    std::variant<TextureAssetHandle, MeshAssetHandle, ShaderAssetHandle, ShaderProgramAssetHandle,
                 MaterialTemplateAssetHandle, MaterialAssetHandle, ModelAssetHandle>;
struct AssetDependencyVersion
{
    AnyAssetHandle handle;
    AssetContentRevision revision = 0;
};
inline bool operator==(const AssetDependencyVersion& a, const AssetDependencyVersion& b)
{
    return a.handle == b.handle && a.revision == b.revision;
}
} // namespace rubia::asset
