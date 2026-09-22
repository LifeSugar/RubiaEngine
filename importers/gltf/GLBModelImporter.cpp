#include "gltf/GLBModelImporter.hpp"

#include "gltf/GLBTypes.hpp"
#include "gltf/GLBMeshImporter.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace rubia::importer::gltf
{
namespace
{

uint32_t checkedSize(std::size_t size, const char* description)
{
    if (size > std::numeric_limits<uint32_t>::max())
    {
        throw std::overflow_error(
            std::string(description) + " exceeds the uint32_t range");
    }
    return static_cast<uint32_t>(size);
}

uint32_t appendNode(
    const GLBNode& source,
    uint32_t parent,
    const std::vector<asset::MeshAssetHandle>& meshHandles,
    std::vector<asset::ModelNode>& destination)
{
    const uint32_t nodeIndex = checkedSize(
        destination.size(),
        "imported model node index");
    destination.push_back({});

    asset::ModelNode& node = destination[nodeIndex];
    node.name = source.name;
    node.localTransform = source.localTransform;
    node.parent = parent;
    node.meshes.reserve(source.meshIndices.size());
    for (int sourceMeshIndex : source.meshIndices)
    {
        if (sourceMeshIndex < 0 ||
            static_cast<std::size_t>(sourceMeshIndex) >= meshHandles.size())
        {
            throw std::invalid_argument(
                "GLB node references a mesh outside the document");
        }
        node.meshes.push_back(
            meshHandles[static_cast<std::size_t>(sourceMeshIndex)]);
    }

    for (const GLBNode& child : source.children)
    {
        appendNode(child, nodeIndex, meshHandles, destination);
    }
    return nodeIndex;
}

enum TextureColorUsage : uint8_t
{
    TextureColorUsageNone = 0,
    TextureColorUsageLinear = 1u << 0u,
    TextureColorUsageSrgb = 1u << 1u
};

void addTextureColorUsage(
    std::vector<uint8_t>& usages,
    int textureIndex,
    TextureColorUsage usage)
{
    if (textureIndex < 0)
    {
        return;
    }
    if (static_cast<std::size_t>(textureIndex) >= usages.size())
    {
        throw std::invalid_argument(
            "GLB material texture index is outside the texture table");
    }
    usages[static_cast<std::size_t>(textureIndex)] |= usage;
}

std::vector<asset::TextureColorSpace> resolveTextureColorSpaces(
    const GLBModel& source,
    asset::TextureColorSpace fallback)
{
    std::vector<uint8_t> usages(
        source.textures.size(),
        TextureColorUsageNone);
    for (const GLBMaterial& material : source.materials)
    {
        addTextureColorUsage(
            usages,
            material.baseColorTextureIndex,
            TextureColorUsageSrgb);
        addTextureColorUsage(
            usages,
            material.emissiveTextureIndex,
            TextureColorUsageSrgb);
        addTextureColorUsage(
            usages,
            material.metallicRoughnessTextureIndex,
            TextureColorUsageLinear);
        addTextureColorUsage(
            usages,
            material.normalTextureIndex,
            TextureColorUsageLinear);
        addTextureColorUsage(
            usages,
            material.occlusionTextureIndex,
            TextureColorUsageLinear);
    }

    std::vector<asset::TextureColorSpace> result(source.textures.size(), fallback);
    for (std::size_t index = 0; index < usages.size(); ++index)
    {
        if (usages[index] ==
            (TextureColorUsageLinear | TextureColorUsageSrgb))
        {
            throw std::invalid_argument(
                "one GLB texture is used by both linear and sRGB material semantics");
        }
        if ((usages[index] & TextureColorUsageLinear) != 0)
        {
            result[index] = asset::TextureColorSpace::Linear;
        }
        else if ((usages[index] & TextureColorUsageSrgb) != 0)
        {
            result[index] = asset::TextureColorSpace::Srgb;
        }
    }
    return result;
}

} // namespace

GLBModelImporter::Result GLBModelImporter::import(
    const GLBModel& source,
    const CreateInfo& createInfo) const
{
    if (createInfo.assets == nullptr)
    {
        throw std::invalid_argument(
            "GLBModelImporter requires an AssetManager");
    }

    if (createInfo.checkpoint) createInfo.checkpoint();
    Result result{};

    const std::vector<asset::TextureColorSpace> textureColorSpaces =
        resolveTextureColorSpaces(source, createInfo.textureColorSpace);

    GLBTextureImporter::CreateInfo textureInfo{};
    textureInfo.assets = createInfo.assets;
    textureInfo.baseDirectory = createInfo.baseDirectory;
    textureInfo.colorSpace = createInfo.textureColorSpace;
    textureInfo.colorSpaces = &textureColorSpaces;
    textureInfo.sampler = createInfo.textureSampler;
    textureInfo.fallbackTexture = createInfo.defaultTexture;
    textureInfo.decoder = createInfo.textureDecoder;
    result.textures = GLBTextureImporter{}.import(
        source.textures,
        textureInfo);

    if (createInfo.checkpoint) createInfo.checkpoint();
    if (!source.materials.empty())
    {
        GLBMaterialImporter::CreateInfo materialInfo{};
        materialInfo.assets = createInfo.assets;
        materialInfo.mapping = createInfo.materialMapping;
        materialInfo.textures = &result.textures;
        materialInfo.defaultTexture = createInfo.defaultTexture;
        materialInfo.defaultDataTexture =
            createInfo.defaultDataTexture;
        materialInfo.defaultNormalTexture =
            createInfo.defaultNormalTexture;
        result.materials = GLBMaterialImporter{}.import(
            source.materials,
            materialInfo);
    }

    if (createInfo.checkpoint) createInfo.checkpoint();
    GLBMeshImporter::CreateInfo meshInfo{};
    meshInfo.assets = createInfo.assets;
    meshInfo.materials = &result.materials;
    meshInfo.fallbackMaterial = createInfo.fallbackMaterial;
    result.meshes = GLBMeshImporter{}.import(source.meshes, meshInfo);

    if (createInfo.checkpoint) createInfo.checkpoint();
    asset::ModelAsset::CreateInfo modelInfo{};
    modelInfo.name = source.name;
    appendNode(
        source.rootNode,
        asset::kInvalidModelNodeIndex,
        result.meshes,
        modelInfo.nodes);
    result.model = createInfo.assets->createModel(std::move(modelInfo));
    return result;
}

} // namespace rubia::importer::gltf
