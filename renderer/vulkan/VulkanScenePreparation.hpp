#pragma once

#include "asset/AssetSnapshot.hpp"
#include "render/SceneResourcePreparation.hpp"
#include "render/PipelinePreparationTypes.hpp"
#include "vulkan/ResourcePreparationTypes.hpp"
#include <exception>
#include <optional>
#include <vector>

namespace rubia::rhi::vulkan
{
class RenderAssetCache;
class VulkanRenderer;
class VulkanResourcePreparation;

// Coordinates generic asset preparations, pipeline creation and scene activation.
// It owns no upload requests, command pools, transfer queues or fences.
// Renderer, cache and preparation manager outlive this render-thread-only session.
class VulkanScenePreparation final
{
public:
    VulkanScenePreparation(VulkanRenderer& renderer, RenderAssetCache& cache,
                           VulkanResourcePreparation& preparations,
                           render::SceneResourceRequest request);
    ~VulkanScenePreparation();
    VulkanScenePreparation(const VulkanScenePreparation&) = delete;
    VulkanScenePreparation& operator=(const VulkanScenePreparation&) = delete;

    void begin();
    void advance();
    void activate();
    void cancel() noexcept;
    [[nodiscard]] render::ScenePreparationStatus status() const
    {
        return status_;
    }
    [[nodiscard]] bool ownsResources() const noexcept
    {
        return ownsResources_;
    }

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
    void fail(const std::exception& error);

    VulkanRenderer& renderer_;
    RenderAssetCache& cache_;
    VulkanResourcePreparation& preparations_;
    render::SceneResourceRequest request_;
    render::ScenePreparationStatus status_;
    std::vector<Root> roots_;
    bool ownsResources_ = false;
    bool sceneResourcesCreated_ = false;
    render::PipelinePreparationTicket pipelines_;
};
} // namespace rubia::rhi::vulkan
