#include "content/DemoContent.hpp"

#include "asset/AssetManager.hpp"
#include "gltf/GLBLoader.hpp"
#include "gltf/GLBModelImporter.hpp"
#include "shader/SpirvShaderImporter.hpp"
#include "texture/StbImageDecoder.hpp"
#include "texture/TextureImportRegistry.hpp"
#include "scene/Scene.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace rubia::editor
{
namespace
{

enum DemoTextureUsage : uint8_t
{
    DemoTextureUsageNone = 0,
    DemoTextureUsageColor = 1u << 0u,
    DemoTextureUsageData = 1u << 1u,
    DemoTextureUsageNormal = 1u << 2u
};

void addDemoTextureUsage(
    std::vector<uint8_t>& usages,
    int textureIndex,
    DemoTextureUsage usage)
{
    if (textureIndex < 0)
    {
        return;
    }
    if (static_cast<std::size_t>(textureIndex) >= usages.size())
    {
        throw std::invalid_argument(
            "demo material texture index is outside the texture table");
    }
    usages[static_cast<std::size_t>(textureIndex)] |= usage;
}

std::vector<uint8_t> resolveDemoTextureUsages(const importer::gltf::GLBModel& model)
{
    std::vector<uint8_t> usages(
        model.textures.size(),
        DemoTextureUsageNone);
    for (const importer::gltf::GLBMaterial& material : model.materials)
    {
        addDemoTextureUsage(
            usages,
            material.baseColorTextureIndex,
            DemoTextureUsageColor);
        addDemoTextureUsage(
            usages,
            material.emissiveTextureIndex,
            DemoTextureUsageColor);
        addDemoTextureUsage(
            usages,
            material.metallicRoughnessTextureIndex,
            DemoTextureUsageData);
        addDemoTextureUsage(
            usages,
            material.occlusionTextureIndex,
            DemoTextureUsageData);
        addDemoTextureUsage(
            usages,
            material.normalTextureIndex,
            DemoTextureUsageNormal);
    }
    return usages;
}

importer::texture::TextureImportSettings makeDemoTextureImportSettings(
    const DemoContentLoader::TextureImportPolicy& policy,
    asset::TextureColorSpace colorSpace,
    uint8_t usage)
{
    importer::texture::TextureImportSettings settings{};
    settings.colorSpace = colorSpace;
    settings.generateMipmaps = policy.generateMipmaps;
    settings.basis.encoding = policy.payloadEncoding;
    settings.highQualityTranscode = policy.highQualityTranscode;

    // BC5 is valid only when the image is exclusively a normal map. A shared
    // normal/data image keeps all channels and uses the conservative data
    // format instead.
    if (usage == DemoTextureUsageNormal)
    {
        settings.colorSpace = asset::TextureColorSpace::Linear;
        settings.basis.normalMap = true;
        settings.transcodeFormat = policy.normalTranscodeFormat;
    }
    else if (usage == DemoTextureUsageColor ||
             (usage == DemoTextureUsageNone &&
              colorSpace == asset::TextureColorSpace::Srgb))
    {
        settings.transcodeFormat = policy.colorTranscodeFormat;
    }
    else
    {
        settings.transcodeFormat = policy.dataTranscodeFormat;
    }
    return settings;
}

std::filesystem::path resolveAssetPath(
    const std::filesystem::path& cookedAssetDirectory,
    const std::filesystem::path& assetPath)
{
    const std::filesystem::path configuredPath = assetPath.is_absolute()
        ? assetPath
        : cookedAssetDirectory / assetPath;
    if (std::filesystem::exists(configuredPath))
    {
        return configuredPath;
    }

    const std::filesystem::path sourcePath =
        std::filesystem::path(PROJECT_SOURCE_DIR) / configuredPath;
    if (std::filesystem::exists(sourcePath))
    {
        return sourcePath;
    }

    return configuredPath;
}

void validateCreateInfo(const DemoContentLoader::CreateInfo& createInfo)
{
    if (createInfo.pbrVertexShader.empty() ||
        createInfo.pbrFragmentShader.empty() ||
        createInfo.presentVertexShader.empty() ||
        createInfo.presentFragmentShader.empty() ||
        createInfo.cookedAssetDirectory.empty())
    {
        throw std::invalid_argument(
            "DemoContentLoader requires a cooked asset directory and all demo asset paths");
    }
}

} // namespace

DemoContent DemoContentLoader::load(
    asset::AssetManager& assets,
    scene::Scene& scene,
    const CreateInfo& createInfo,
    importer::texture::TextureImportRegistry* textureImports,
    const std::function<void()>& checkpoint)
{
    if (checkpoint) checkpoint();
    DemoContent builtins = loadBuiltins(assets, createInfo);
    return loadModel(assets, scene, createInfo, builtins, textureImports, checkpoint);
}

DemoContent DemoContentLoader::loadBuiltins(
    asset::AssetManager& assets, const CreateInfo& createInfo)
{
    validateCreateInfo(createInfo);
    DemoContent content{};

    asset::TextureAsset::CreateInfo textureInfo{};
    textureInfo.name = "Default White";
    textureInfo.width = 1;
    textureInfo.height = 1;
    textureInfo.format = asset::TextureFormat::RGBA8UNorm;
    textureInfo.colorSpace = asset::TextureColorSpace::Srgb;
    textureInfo.payload = {
        std::byte{0xff},
        std::byte{0xff},
        std::byte{0xff},
        std::byte{0xff}
    };
    content.defaultTexture = assets.createTexture(std::move(textureInfo));

    asset::TextureAsset::CreateInfo dataTextureInfo{};
    dataTextureInfo.name = "Default Linear White";
    dataTextureInfo.width = 1;
    dataTextureInfo.height = 1;
    dataTextureInfo.format = asset::TextureFormat::RGBA8UNorm;
    dataTextureInfo.colorSpace = asset::TextureColorSpace::Linear;
    dataTextureInfo.payload = {
        std::byte{0xff},
        std::byte{0xff},
        std::byte{0xff},
        std::byte{0xff}
    };
    content.defaultDataTexture =
        assets.createTexture(std::move(dataTextureInfo));

    asset::TextureAsset::CreateInfo normalTextureInfo{};
    normalTextureInfo.name = "Default Flat Normal";
    normalTextureInfo.width = 1;
    normalTextureInfo.height = 1;
    normalTextureInfo.format = asset::TextureFormat::RGBA8UNorm;
    normalTextureInfo.colorSpace = asset::TextureColorSpace::Linear;
    normalTextureInfo.payload = {
        std::byte{0x80},
        std::byte{0x80},
        std::byte{0xff},
        std::byte{0xff}
    };
    content.defaultNormalTexture =
        assets.createTexture(std::move(normalTextureInfo));

    importer::shader::SpirvShaderImporter shaderImporter;
    importer::shader::SpirvShaderImporter::CreateInfo shaderInfo{};
    shaderInfo.assets = &assets;
    shaderInfo.path = resolveAssetPath(
        createInfo.cookedAssetDirectory,
        createInfo.pbrVertexShader);
    shaderInfo.name = "PBR Vertex";
    shaderInfo.stage = asset::ShaderStage::Vertex;
    content.pbrVertexShader = shaderImporter.import(shaderInfo);

    shaderInfo.path = resolveAssetPath(
        createInfo.cookedAssetDirectory,
        createInfo.pbrFragmentShader);
    shaderInfo.name = "PBR Fragment";
    shaderInfo.stage = asset::ShaderStage::Fragment;
    content.pbrFragmentShader = shaderImporter.import(shaderInfo);

    shaderInfo.path = resolveAssetPath(
        createInfo.cookedAssetDirectory,
        createInfo.presentVertexShader);
    shaderInfo.name = "Present Vertex";
    shaderInfo.stage = asset::ShaderStage::Vertex;
    content.presentVertexShader = shaderImporter.import(shaderInfo);

    shaderInfo.path = resolveAssetPath(
        createInfo.cookedAssetDirectory,
        createInfo.presentFragmentShader);
    shaderInfo.name = "Present Fragment";
    shaderInfo.stage = asset::ShaderStage::Fragment;
    content.presentFragmentShader = shaderImporter.import(shaderInfo);

    asset::MaterialTemplateAsset::CreateInfo templateInfo{};
    templateInfo.name = "glTF Metallic-Roughness PBR";
    content.pbrProgram = assets.createShaderProgram({
        "PBR", {content.pbrVertexShader, content.pbrFragmentShader}});
    content.presentProgram = assets.createShaderProgram({
        "Present", {content.presentVertexShader, content.presentFragmentShader}});
    templateInfo.program = content.pbrProgram;
    templateInfo.textureSlots = {
        {"baseColorTexture", "baseColorTexture", "baseColorSampler"},
        {"metallicRoughnessTexture", "metallicRoughnessTexture", "metallicRoughnessSampler"},
        {"normalTexture", "normalTexture", "normalSampler"},
        {"occlusionTexture", "occlusionTexture", "occlusionSampler"},
        {"emissiveTexture", "emissiveTexture", "emissiveSampler"}
    };
    content.materialTemplate =
        assets.createMaterialTemplate(std::move(templateInfo));

    asset::MaterialAsset::CreateInfo materialInfo{};
    materialInfo.name = "Default PBR Material";
    materialInfo.materialTemplate = content.materialTemplate;
    materialInfo.parameters = {
        {"baseColorFactor", glm::vec4(1.0f)},
        {"emissiveFactor", glm::vec3(0.0f)},
        {"metallicFactor", 0.0f},
        {"roughnessFactor", 1.0f}
    };
    materialInfo.textures = {
        {"baseColorTexture", content.defaultTexture},
        {"metallicRoughnessTexture", content.defaultDataTexture},
        {"normalTexture", content.defaultNormalTexture},
        {"occlusionTexture", content.defaultDataTexture},
        {"emissiveTexture", content.defaultTexture}
    };
    content.defaultMaterial =
        assets.createMaterial(std::move(materialInfo));

    return content;
}

DemoContent DemoContentLoader::loadModel(
    asset::AssetManager& assets, scene::Scene& scene,
    const CreateInfo& createInfo, DemoContent content,
    importer::texture::TextureImportRegistry* textureImports,
    const std::function<void()>& checkpoint)
{
    if (createInfo.modelPath.empty())
    {
        throw std::invalid_argument("DemoContentLoader requires a model path");
    }
    if (checkpoint) checkpoint();
    const std::filesystem::path resolvedModelPath = resolveAssetPath(
        createInfo.cookedAssetDirectory,
        createInfo.modelPath);
    importer::gltf::GLBLoader loader;
    std::unique_ptr<importer::gltf::GLBModel> sourceModel =
        loader.load(resolvedModelPath.string());
    if (!sourceModel)
    {
        throw std::runtime_error(
            "Failed to load model: " + createInfo.modelPath.string() + "\n" +
            loader.getLastError());
    }

    if (checkpoint) checkpoint();
    importer::gltf::GLBModelImporter::CreateInfo importerInfo{};
    importerInfo.checkpoint = checkpoint;
    importerInfo.assets = &assets;
    importerInfo.baseDirectory =
        resolvedModelPath.parent_path();
    importerInfo.defaultTexture = content.defaultTexture;
    importerInfo.defaultDataTexture = content.defaultDataTexture;
    importerInfo.defaultNormalTexture = content.defaultNormalTexture;
    importer::texture::StbImageDecoder imageDecoder;
    importerInfo.textureDecoder =
        [&imageDecoder, &checkpoint](
            const importer::gltf::GLBTexture& texture,
            const std::filesystem::path& baseDirectory)
        {
            if (checkpoint) checkpoint();
            if (texture.storage == importer::gltf::GLBTextureStorage::EncodedBytes)
            {
                return imageDecoder.decodeMemory(
                    texture.data,
                    texture.name);
            }
            if (texture.storage == importer::gltf::GLBTextureStorage::ExternalUri)
            {
                return imageDecoder.decodeFile(
                    baseDirectory / texture.uri,
                    texture.name);
            }
            throw std::invalid_argument(
                "unsupported GLB texture payload passed to the image decoder");
        };
    importerInfo.materialMapping.materialTemplate = content.materialTemplate;
    importerInfo.materialMapping.baseColorParameter = "baseColorFactor";
    importerInfo.materialMapping.emissiveParameter = "emissiveFactor";
    importerInfo.materialMapping.metallicParameter = "metallicFactor";
    importerInfo.materialMapping.roughnessParameter = "roughnessFactor";
    importerInfo.materialMapping.baseColorTextureSlot = "baseColorTexture";
    importerInfo.materialMapping.metallicRoughnessTextureSlot =
        "metallicRoughnessTexture";
    importerInfo.materialMapping.normalTextureSlot = "normalTexture";
    importerInfo.materialMapping.occlusionTextureSlot = "occlusionTexture";
    importerInfo.materialMapping.emissiveTextureSlot = "emissiveTexture";
    importerInfo.fallbackMaterial = content.defaultMaterial;

    importer::gltf::GLBModelImporter importer;
    importer::gltf::GLBModelImporter::Result importedModel =
        importer.import(*sourceModel, importerInfo);

    if (checkpoint) checkpoint();
    if (textureImports != nullptr)
    {
        if (sourceModel->textures.size() != importedModel.textures.size())
        {
            throw std::runtime_error(
                "imported texture handles do not match the source texture table");
        }
        const std::vector<uint8_t> textureUsages =
            resolveDemoTextureUsages(*sourceModel);
        for (std::size_t index = 0;
             index < sourceModel->textures.size();
             ++index)
        {
            const importer::gltf::GLBTexture& sourceTexture = sourceModel->textures[index];
            if (sourceTexture.storage != importer::gltf::GLBTextureStorage::ExternalUri)
            {
                continue;
            }

            const std::filesystem::path sourcePath =
                (resolvedModelPath.parent_path() / sourceTexture.uri)
                    .lexically_normal();
            std::filesystem::path cookedFilename =
                std::filesystem::path(sourceTexture.uri).filename();
            cookedFilename.replace_extension(".ktx2");

            importer::texture::TextureImportRecord record{};
            record.texture = importedModel.textures[index];
            record.sourcePath = sourcePath;
            record.cookedPath = resolvedModelPath.parent_path() /
                "_cooked" / cookedFilename;
            record.settings = makeDemoTextureImportSettings(
                createInfo.textureImportPolicy,
                assets.texture(record.texture).colorSpace(),
                textureUsages[index]);
            textureImports->registerTexture(std::move(record));
        }
    }
    if (importedModel.meshes.empty())
    {
        throw std::runtime_error(
            std::string("Imported model contains no renderer meshes: ") +
            createInfo.modelPath.string());
    }
    content.model = importedModel.model;
    const asset::ModelAsset& demoModel = assets.model(content.model);

    std::clog
        << "[Assets] Imported " << createInfo.modelPath.string()
        << ": textures=" << importedModel.textures.size()
        << ", materials=" << importedModel.materials.size()
        << ", meshes=" << importedModel.meshes.size()
        << ", nodes=" << demoModel.nodes().size()
        << '\n';

    scene::Scene::CreateInfo sceneInfo{};
    sceneInfo.name = "Demo Scene";
    scene::SceneNode sceneRoot{};
    sceneRoot.name = demoModel.name();
    sceneRoot.model = content.model;
    sceneInfo.nodes.push_back(std::move(sceneRoot));
    scene.create(std::move(sceneInfo));

    return content;
}

} // namespace rubia::editor
