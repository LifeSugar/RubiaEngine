#include "render/SceneResourcePreparation.hpp"
#include "asset/AssetManager.hpp"
#include <set>
#include <stdexcept>

namespace rubia::render
{
std::vector<asset::MeshAssetHandle> collectModelMeshes(
    const asset::AssetManager& assets, const std::vector<asset::ModelAssetHandle>& models)
{
    std::vector<asset::MeshAssetHandle> result;
    std::set<std::pair<uint32_t, uint32_t>> seen;
    for (auto model : models)
    {
        for (const auto& node : assets.model(model).nodes())
        {
            for (auto mesh : node.meshes)
            {
                if (!assets.contains(mesh))
                {
                    throw std::invalid_argument("model references an invalid mesh");
                }
                if (seen.emplace(mesh.index, mesh.generation).second)
                {
                    result.push_back(mesh);
                }
            }
        }
    }
    return result;
}
SceneResourceRequest makeSceneResourceRequest(const asset::AssetManager& assets,
                                              const std::vector<asset::ModelAssetHandle>& models,
                                              asset::MaterialTemplateAssetHandle materialTemplate,
                                              asset::ShaderProgramAssetHandle presentProgram,
                                              uint32_t maxRenderObjects)
{
    if (models.empty() || !maxRenderObjects)
    {
        throw std::invalid_argument("scene requires models and object capacity");
    }
    SceneResourceRequest request;
    request.materialTemplate = assets.snapshot(materialTemplate);
    request.presentProgram = assets.snapshot(presentProgram);
    request.maxRenderObjects = maxRenderObjects;
    for (auto mesh : collectModelMeshes(assets, models))
    {
        // Current scene pipeline uses one material interface; this is a frontend policy.
        for (const auto& submesh : assets.mesh(mesh).submeshes())
        {
            if (assets.material(submesh.material).materialTemplate() != materialTemplate)
            {
                throw std::invalid_argument(
                    "scene requires the requested shared material template");
            }
        }
        request.meshes.push_back(assets.snapshot(mesh));
    }
    return request;
}
} // namespace rubia::render
