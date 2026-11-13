#pragma once

#include "asset/AssetSnapshot.hpp"
#include "render/SceneResourcePreparation.hpp"
#include "render/PipelinePreparationTypes.hpp"
#include "resources/ResourcePreparationService.hpp"
#include "resources/PreparedAssets.hpp"
#include <exception>
#include <optional>
#include <vector>

namespace rubia::render
{
class RenderAssetStore;
class ScenePreparationHost
{
  public:
    virtual ~ScenePreparationHost() = default;
    virtual void createPreparedSceneResources(std::shared_ptr<const PreparedMaterialTemplate>,
                                              std::shared_ptr<const PreparedShaderProgram>,
                                              uint32_t maxRenderObjects) = 0;
    virtual PipelinePreparationResult
    prewarmScenePipelines(const RenderAssetStore &,
                          const std::vector<asset::MaterialAssetHandle> &) = 0;
    virtual PipelinePreparationStatus
        pipelinePreparationStatus(PipelinePreparationTicket) const = 0;
    virtual void cancelPipelinePreparation(PipelinePreparationTicket) = 0;
    virtual void releasePipelinePreparation(PipelinePreparationTicket) = 0;
    virtual void releasePreparedSceneResources() noexcept = 0;
};

// Coordinates generic asset preparations, pipeline creation and scene activation.
// It owns no upload requests, command pools, transfer queues or fences.
// Renderer, cache and preparation manager outlive this render-thread-only session.
class ScenePreparationSession final
{
  public:
    ScenePreparationSession(ScenePreparationHost &renderer, RenderAssetStore &cache,
                            ResourcePreparationService &preparations,
                            render::SceneResourceRequest request);
    ~ScenePreparationSession();
    ScenePreparationSession(const ScenePreparationSession &) = delete;
    ScenePreparationSession &operator=(const ScenePreparationSession &) = delete;

    void begin();
    void advance();
    void activate();
    void cancel() noexcept;
    [[nodiscard]] render::ScenePreparationStatus status() const { return status_; }
    [[nodiscard]] bool ownsResources() const noexcept { return ownsResources_; }

  private:
    struct Root
    {
        using Source = std::variant<asset::AssetSnapshot<asset::MeshAsset>,
                                    asset::AssetSnapshot<asset::MaterialTemplateAsset>,
                                    asset::AssetSnapshot<asset::ShaderProgramAsset>>;
        std::optional<Source> source;
        ResourcePreparationTicket ticket;
        bool ready = false;
    };
    void discardResources() noexcept;
    void fail(const std::exception &error);

    ScenePreparationHost &renderer_;
    RenderAssetStore &cache_;
    ResourcePreparationService &preparations_;
    render::SceneResourceRequest request_;
    render::ScenePreparationStatus status_;
    std::vector<Root> roots_;
    bool ownsResources_ = false;
    bool sceneResourcesCreated_ = false;
    render::PipelinePreparationTicket pipelines_;
};
} // namespace rubia::render
