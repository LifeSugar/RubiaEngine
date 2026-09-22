#include "gltf/GLBMaterialImporter.hpp"

#include <cstddef>
#include <stdexcept>
#include <utility>

namespace rubia::importer::gltf
{
namespace
{

asset::TextureAssetHandle resolveTexture(
    int sourceIndex,
    const std::vector<asset::TextureAssetHandle>* textures,
    asset::TextureAssetHandle defaultTexture)
{
    if (sourceIndex < 0)
    {
        return defaultTexture;
    }
    if (textures == nullptr ||
        static_cast<std::size_t>(sourceIndex) >= textures->size())
    {
        throw std::invalid_argument(
            "GLB material references a texture outside the imported texture table");
    }
    return (*textures)[static_cast<std::size_t>(sourceIndex)];
}

void addTextureAssignment(
    asset::MaterialAsset::CreateInfo& destination,
    const std::string& slotName,
    int sourceIndex,
    asset::TextureAssetHandle fallbackTexture,
    const GLBMaterialImporter::CreateInfo& createInfo)
{
    if (slotName.empty())
    {
        return;
    }

    const asset::TextureAssetHandle texture = resolveTexture(
        sourceIndex,
        createInfo.textures,
        fallbackTexture);
    if (texture)
    {
        destination.textures.push_back({slotName, texture});
    }
}

} // namespace

std::vector<asset::MaterialAssetHandle> GLBMaterialImporter::import(
    const std::vector<GLBMaterial>& source,
    const CreateInfo& createInfo) const
{
    if (createInfo.assets == nullptr)
    {
        throw std::invalid_argument(
            "GLBMaterialImporter requires an AssetManager");
    }
    if (!createInfo.assets->contains(createInfo.mapping.materialTemplate))
    {
        throw std::invalid_argument(
            "GLBMaterialImporter requires a material template owned by its AssetManager");
    }
    if (createInfo.defaultTexture &&
        !createInfo.assets->contains(createInfo.defaultTexture))
    {
        throw std::invalid_argument(
            "GLBMaterialImporter default texture is not owned by its AssetManager");
    }
    if (createInfo.defaultNormalTexture &&
        !createInfo.assets->contains(createInfo.defaultNormalTexture))
    {
        throw std::invalid_argument(
            "GLBMaterialImporter default normal texture is not owned by its AssetManager");
    }
    if (createInfo.defaultDataTexture &&
        !createInfo.assets->contains(createInfo.defaultDataTexture))
    {
        throw std::invalid_argument(
            "GLBMaterialImporter default data texture is not owned by its AssetManager");
    }

    std::vector<asset::MaterialAssetHandle> result;
    result.reserve(source.size());
    for (const GLBMaterial& material : source)
    {
        asset::MaterialAsset::CreateInfo materialInfo{};
        materialInfo.name = material.name;
        materialInfo.materialTemplate = createInfo.mapping.materialTemplate;
        if (material.alphaMode == "BLEND")
            materialInfo.renderState = asset::makeTransparentMaterialState();
        else if (material.alphaMode == "MASK")
        {
            materialInfo.renderState.alphaClipEnabled = true;
            materialInfo.renderState.alphaClipThreshold = material.alphaCutoff;
        }
        else if (material.alphaMode != "OPAQUE")
            throw std::invalid_argument("unsupported glTF material alpha mode: " + material.alphaMode);
        materialInfo.renderState.doubleSided = material.doubleSided;

        if (!createInfo.mapping.baseColorParameter.empty())
        {
            materialInfo.parameters.push_back({
                createInfo.mapping.baseColorParameter,
                material.baseColorFactor
            });
        }
        if (!createInfo.mapping.metallicParameter.empty())
        {
            materialInfo.parameters.push_back({
                createInfo.mapping.metallicParameter,
                material.metallicFactor
            });
        }
        if (!createInfo.mapping.roughnessParameter.empty())
        {
            materialInfo.parameters.push_back({
                createInfo.mapping.roughnessParameter,
                material.roughnessFactor
            });
        }
        if (!createInfo.mapping.emissiveParameter.empty())
        {
            materialInfo.parameters.push_back({
                createInfo.mapping.emissiveParameter,
                material.emissiveFactor
            });
        }

        addTextureAssignment(
            materialInfo,
            createInfo.mapping.baseColorTextureSlot,
            material.baseColorTextureIndex,
            createInfo.defaultTexture,
            createInfo);
        addTextureAssignment(
            materialInfo,
            createInfo.mapping.metallicRoughnessTextureSlot,
            material.metallicRoughnessTextureIndex,
            createInfo.defaultDataTexture
                ? createInfo.defaultDataTexture
                : createInfo.defaultTexture,
            createInfo);
        addTextureAssignment(
            materialInfo,
            createInfo.mapping.normalTextureSlot,
            material.normalTextureIndex,
            createInfo.defaultNormalTexture
                ? createInfo.defaultNormalTexture
                : createInfo.defaultTexture,
            createInfo);
        addTextureAssignment(
            materialInfo,
            createInfo.mapping.occlusionTextureSlot,
            material.occlusionTextureIndex,
            createInfo.defaultDataTexture
                ? createInfo.defaultDataTexture
                : createInfo.defaultTexture,
            createInfo);
        addTextureAssignment(
            materialInfo,
            createInfo.mapping.emissiveTextureSlot,
            material.emissiveTextureIndex,
            createInfo.defaultTexture,
            createInfo);

        result.push_back(
            createInfo.assets->createMaterial(std::move(materialInfo)));
    }
    return result;
}

} // namespace rubia::importer::gltf
