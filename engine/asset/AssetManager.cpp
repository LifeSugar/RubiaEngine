#include "asset/AssetManager.hpp"

#include "asset/MaterialValidation.hpp"
#include "asset/MaterialTemplateBuilder.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace rubia::asset
{
namespace
{

void validateMaterialRenderState(const MaterialRenderState& state)
{
    switch (state.surfaceType)
    {
    case MaterialSurfaceType::Opaque:
    case MaterialSurfaceType::Transparent:
        break;
    default:
        throw std::invalid_argument(
            "material surface type is invalid");
    }

    switch (state.depth.compare)
    {
    case DepthCompare::Never:
    case DepthCompare::Less:
    case DepthCompare::Equal:
    case DepthCompare::LessEqual:
    case DepthCompare::Greater:
    case DepthCompare::NotEqual:
    case DepthCompare::GreaterEqual:
    case DepthCompare::Always:
        break;
    default:
        throw std::invalid_argument(
            "material depth comparison is invalid");
    }

    if (!std::isfinite(state.alphaClipThreshold) ||
        state.alphaClipThreshold < 0.0f ||
        state.alphaClipThreshold > 1.0f)
    {
        throw std::invalid_argument(
            "material alpha clip threshold must be within [0, 1]");
    }
    if (state.alphaClipEnabled && state.transparent())
    {
        throw std::invalid_argument(
            "alpha clipping currently requires an opaque material");
    }
    if (state.depth.writeEnabled && !state.depth.testEnabled)
    {
        throw std::invalid_argument(
            "material cannot write depth while depth testing is disabled");
    }
}

MaterialValueType valueType(const MaterialValue& value)
{
    switch (value.index())
    {
    case 0: return MaterialValueType::Float;
    case 1: return MaterialValueType::Float2;
    case 2: return MaterialValueType::Float3;
    case 3: return MaterialValueType::Float4;
    case 4: return MaterialValueType::Matrix4;
    case 5: return MaterialValueType::Int;
    case 6: return MaterialValueType::UInt;
    case 7: return MaterialValueType::Bool;
    default:
        throw std::logic_error("unsupported material value variant");
    }
}

void copyMaterialValue(
    std::vector<std::byte>& destination,
    uint32_t byteOffset,
    const MaterialValue& value)
{
    std::visit(
        [&](const auto& typedValue)
        {
            using Value = std::decay_t<decltype(typedValue)>;
            if constexpr (std::is_same_v<Value, bool>)
            {
                const uint32_t shaderBool = typedValue ? 1u : 0u;
                std::memcpy(
                    destination.data() + byteOffset,
                    &shaderBool,
                    sizeof(shaderBool));
            }
            else
            {
                std::memcpy(
                    destination.data() + byteOffset,
                    &typedValue,
                    sizeof(typedValue));
            }
        },
        value);
}

} // namespace

TextureAssetHandle AssetManager::createTexture(
    TextureAsset::CreateInfo createInfo)
{
    return textures_.emplace(std::move(createInfo));
}

TextureAsset AssetManager::replaceTexture(
    TextureAssetHandle handle,
    TextureAsset replacement)
{
    if (!replacement)
    {
        throw std::invalid_argument(
            "cannot replace a texture with an incomplete asset");
    }
    return textures_.replace(handle, std::move(replacement));
}

MaterialTemplateAssetHandle AssetManager::createMaterialTemplate(
    MaterialTemplateAsset::CreateInfo createInfo)
{
    ValidationReport report;
    auto compiled = MaterialTemplateBuilder::build(createInfo, *this, report);
    if (!report.valid())
    {
        throw AssetValidationError(std::move(report));
    }
    return materialTemplates_.emplace(std::move(compiled));
}

MaterialAssetHandle AssetManager::createMaterial(
    MaterialAsset::CreateInfo createInfo)
{
    ValidationReport report = validateMaterial(createInfo);
    if (!report.valid())
    {
        throw AssetValidationError(std::move(report));
    }
    validateMaterialRenderState(createInfo.renderState);

    if (!materialTemplates_.contains(createInfo.materialTemplate))
    {
        throw std::invalid_argument(
            "material references a template outside this AssetManager");
    }

    const MaterialTemplateAsset& materialTemplate =
        materialTemplates_.get(createInfo.materialTemplate);

    std::vector<std::byte> parameterData(
        materialTemplate.parameterDataSize(),
        std::byte{0});
    std::vector<bool> assignedParameters(
        materialTemplate.parameters().size(),
        false);

    for (const MaterialParameterAssignment& assignment : createInfo.parameters)
    {
        const auto& parameters = materialTemplate.parameters();
        const auto iterator = std::find_if(
            parameters.begin(),
            parameters.end(),
            [&](const MaterialParameterDesc& parameter)
            {
                return parameter.name == assignment.name;
            });
        if (iterator == parameters.end())
        {
            throw std::invalid_argument(
                "material contains a parameter absent from its template");
        }

        const std::size_t parameterIndex =
            static_cast<std::size_t>(iterator - parameters.begin());
        if (assignedParameters[parameterIndex])
        {
            throw std::invalid_argument(
                "material assigns the same parameter more than once");
        }
        if (valueType(assignment.value) != iterator->type)
        {
            throw std::invalid_argument(
                "material parameter type does not match its template");
        }

        copyMaterialValue(
            parameterData,
            iterator->byteOffset,
            assignment.value);
        assignedParameters[parameterIndex] = true;
    }

    for (std::size_t index = 0;
         index < materialTemplate.parameters().size();
         ++index)
    {
        if (materialTemplate.parameters()[index].required &&
            !assignedParameters[index])
        {
            throw std::invalid_argument(
                "material is missing a required parameter");
        }
    }

    uint32_t textureCount = 0;
    for (const MaterialTextureSlotDesc& slot : materialTemplate.textureSlots())
    {
        textureCount = std::max(textureCount, slot.slot + 1);
    }
    std::vector<TextureAssetHandle> textures(textureCount);
    std::vector<bool> assignedTextures(textureCount, false);

    for (const MaterialTextureAssignment& assignment : createInfo.textures)
    {
        if (!textures_.contains(assignment.texture))
        {
            throw std::invalid_argument(
                "material references a texture outside this AssetManager");
        }

        const auto& slots = materialTemplate.textureSlots();
        const auto iterator = std::find_if(
            slots.begin(),
            slots.end(),
            [&](const MaterialTextureSlotDesc& slot)
            {
                return slot.name == assignment.name;
            });
        if (iterator == slots.end())
        {
            throw std::invalid_argument(
                "material contains a texture absent from its template");
        }
        if (assignedTextures[iterator->slot])
        {
            throw std::invalid_argument(
                "material assigns the same texture slot more than once");
        }

        textures[iterator->slot] = assignment.texture;
        assignedTextures[iterator->slot] = true;
    }

    for (const MaterialTextureSlotDesc& slot : materialTemplate.textureSlots())
    {
        if (slot.required && !assignedTextures[slot.slot])
        {
            throw std::invalid_argument(
                "material is missing a required texture");
        }
    }

    MaterialAsset::CompiledCreateInfo compiled{};
    compiled.name = std::move(createInfo.name);
    compiled.materialTemplate = createInfo.materialTemplate;
    compiled.renderState = createInfo.renderState;
    compiled.parameterData = std::move(parameterData);
    compiled.textures = std::move(textures);
    return materials_.insert(MaterialAsset(std::move(compiled)));
}

ValidationReport AssetManager::validateMaterialTemplate(
    const MaterialTemplateAsset::CreateInfo& createInfo) const
{
    return validateMaterialTemplateCreateInfo(createInfo, *this);
}

ValidationReport AssetManager::validateMaterial(
    const MaterialAsset::CreateInfo& createInfo) const
{
    return validateMaterialCreateInfo(createInfo, *this);
}

MeshAssetHandle AssetManager::createMesh(MeshAsset::CreateInfo createInfo)
{
    for (const SubmeshData& submesh : createInfo.submeshes)
    {
        if (submesh.material && !materials_.contains(submesh.material))
        {
            throw std::invalid_argument(
                "mesh references a material outside this AssetManager");
        }
    }
    return meshes_.emplace(std::move(createInfo));
}

MeshAsset AssetManager::replaceMesh(MeshAssetHandle handle, MeshAsset replacement)
{
    if (!replacement)
    {
        throw std::invalid_argument("cannot replace mesh with an incomplete asset");
    }
    for (const auto& submesh : replacement.submeshes())
    {
        if (submesh.material && !materials_.contains(submesh.material))
        {
            throw std::invalid_argument("mesh references a material outside this AssetManager");
        }
    }
    return meshes_.replace(handle, std::move(replacement));
}

ShaderAssetHandle AssetManager::createShader(
    ShaderAsset::CreateInfo createInfo)
{
    return shaders_.emplace(std::move(createInfo));
}

ShaderProgramAssetHandle AssetManager::createShaderProgram(
    ShaderProgramAsset::CreateInfo createInfo)
{
    return shaderPrograms_.insert(ShaderProgramBuilder::build(std::move(createInfo), *this));
}

const ShaderProgramAsset& AssetManager::shaderProgram(ShaderProgramAssetHandle handle) const
{
    return shaderPrograms_.get(handle);
}

bool AssetManager::contains(ShaderProgramAssetHandle handle) const noexcept
{
    return shaderPrograms_.contains(handle);
}

ModelAssetHandle AssetManager::createModel(ModelAsset::CreateInfo createInfo)
{
    for (const ModelNode& node : createInfo.nodes)
    {
        for (MeshAssetHandle mesh : node.meshes)
        {
            if (!meshes_.contains(mesh))
            {
                throw std::invalid_argument(
                    "model references a mesh outside this AssetManager");
            }
        }
    }
    return models_.emplace(std::move(createInfo));
}

const TextureAsset& AssetManager::texture(TextureAssetHandle handle) const
{
    return textures_.get(handle);
}

const MaterialTemplateAsset& AssetManager::materialTemplate(
    MaterialTemplateAssetHandle handle) const
{
    return materialTemplates_.get(handle);
}

const MaterialAsset& AssetManager::material(MaterialAssetHandle handle) const
{
    return materials_.get(handle);
}

const MeshAsset& AssetManager::mesh(MeshAssetHandle handle) const
{
    return meshes_.get(handle);
}

const ShaderAsset& AssetManager::shader(ShaderAssetHandle handle) const
{
    return shaders_.get(handle);
}

const ModelAsset& AssetManager::model(ModelAssetHandle handle) const
{
    return models_.get(handle);
}

bool AssetManager::contains(TextureAssetHandle handle) const noexcept
{
    return textures_.contains(handle);
}

bool AssetManager::contains(MaterialTemplateAssetHandle handle) const noexcept
{
    return materialTemplates_.contains(handle);
}

bool AssetManager::contains(MaterialAssetHandle handle) const noexcept
{
    return materials_.contains(handle);
}

bool AssetManager::contains(MeshAssetHandle handle) const noexcept
{
    return meshes_.contains(handle);
}

bool AssetManager::contains(ShaderAssetHandle handle) const noexcept
{
    return shaders_.contains(handle);
}

bool AssetManager::contains(ModelAssetHandle handle) const noexcept
{
    return models_.contains(handle);
}

AssetContentRevision AssetManager::contentRevision(TextureAssetHandle handle) const
{
    return textures_.contentRevision(handle);
}

AssetContentRevision AssetManager::contentRevision(MaterialTemplateAssetHandle handle) const
{
    return materialTemplates_.contentRevision(handle);
}

AssetContentRevision AssetManager::contentRevision(MaterialAssetHandle handle) const
{
    return materials_.contentRevision(handle);
}

AssetContentRevision AssetManager::contentRevision(MeshAssetHandle handle) const
{
    return meshes_.contentRevision(handle);
}

AssetContentRevision AssetManager::contentRevision(ShaderAssetHandle handle) const
{
    return shaders_.contentRevision(handle);
}

AssetContentRevision AssetManager::contentRevision(ShaderProgramAssetHandle handle) const
{
    return shaderPrograms_.contentRevision(handle);
}

AssetContentRevision AssetManager::contentRevision(ModelAssetHandle handle) const
{
    return models_.contentRevision(handle);
}

bool AssetManager::isMaterialTemplateCurrent(
    MaterialTemplateAssetHandle handle) const noexcept
{
    if (!materialTemplates_.contains(handle))
    {
        return false;
    }
    const MaterialTemplateAsset& materialTemplate =
        materialTemplates_.get(handle);
    return contains(materialTemplate.program()) &&
        shaderProgram(materialTemplate.program()).interfaceSignature() ==
            materialTemplate.programInterfaceSignature();
}

std::vector<TextureAssetHandle> AssetManager::textureHandles() const
{
    return textures_.handles();
}

std::vector<MaterialAssetHandle> AssetManager::materialHandles() const
{
    return materials_.handles();
}

std::vector<ModelAssetHandle> AssetManager::modelHandles() const
{
    return models_.handles();
}

void AssetManager::reset() noexcept
{
    models_.reset();
    meshes_.reset();
    materials_.reset();
    materialTemplates_.reset();
    shaderPrograms_.reset();
    shaders_.reset();
    textures_.reset();
}

} // namespace rubia::asset
