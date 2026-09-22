#pragma once

#include "asset/AssetSnapshot.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace rubia::asset
{
class AssetManager;
}

namespace rubia::render
{

// CPU render frontend expands models into concrete, immutable resource inputs.
// This request contains no model hierarchy or AssetManager owner.
struct SceneResourceRequest
{
    std::vector<asset::AssetSnapshot<asset::MeshAsset>> meshes;
    // Bootstrap pipeline only; individual meshes may depend on other templates.
    asset::AssetSnapshot<asset::MaterialTemplateAsset> materialTemplate;
    asset::AssetSnapshot<asset::ShaderProgramAsset> presentProgram;
    uint32_t maxRenderObjects = 1024;
};

std::vector<asset::MeshAssetHandle> collectModelMeshes(
    const asset::AssetManager& assets, const std::vector<asset::ModelAssetHandle>& models);
SceneResourceRequest makeSceneResourceRequest(const asset::AssetManager& assets,
                                              const std::vector<asset::ModelAssetHandle>& models,
                                              asset::MaterialTemplateAssetHandle materialTemplate,
                                              asset::ShaderProgramAssetHandle presentProgram,
                                              uint32_t maxRenderObjects = 1024);

enum class ScenePreparationState
{
    Idle,
    PreparingResources,
    PreparingPipelines,
    Ready,
    Activated,
    Cancelled,
    Failed
};

struct ScenePreparationStatus
{
    ScenePreparationState state = ScenePreparationState::Idle;
    std::size_t total = 0;
    // Counts ready root preparations (meshes, material template and present program),
    // including all their dependencies. This is not GPU transfer progress.
    std::size_t completed = 0;
    std::size_t pipelinesTotal = 0;
    std::size_t pipelinesCompleted = 0;
    std::string error;
};

} // namespace rubia::render
