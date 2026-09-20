#pragma once

#include "asset/AssetFwd.hpp"

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

/// The source must remain immutable during preparation. The backend retains
/// shared ownership until activation, cancellation, or failure.
struct SceneResourceRequest
{
    std::shared_ptr<const asset::AssetManager> assets;
    std::vector<asset::ModelAssetHandle> models;
    asset::MaterialTemplateAssetHandle materialTemplate;
    asset::ShaderProgramAssetHandle presentProgram;
    uint32_t maxRenderObjects = 1024;
};

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
    // Counts ready root preparations (models, material template and present program),
    // including all their dependencies. This is not GPU transfer progress.
    std::size_t completed = 0;
    std::string error;
};

} // namespace rubia::render
