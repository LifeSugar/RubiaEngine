#pragma once

#include "render/SceneResourcePreparation.hpp"
#include "vulkan/VulkanUploadService.hpp"

#include <exception>
#include <memory>

namespace rubia::rhi::vulkan
{
class RenderAssetCache;
class VulkanRenderer;

/// Backend-internal, render-thread-only preparation of an initially empty
/// scene. Renderer, cache, and device must outlive this session.
class VulkanScenePreparation final
{
public:
    VulkanScenePreparation(const Device& device, VulkanRenderer& renderer, RenderAssetCache& cache,
                           VulkanUploadService& uploads, render::SceneResourceRequest request);
    ~VulkanScenePreparation();
    VulkanScenePreparation(const VulkanScenePreparation&) = delete;
    VulkanScenePreparation& operator=(const VulkanScenePreparation&) = delete;

    void begin();
    void advance();
    void activate();
    void cancel() noexcept;
    [[nodiscard]] render::ScenePreparationStatus status() const;
    [[nodiscard]] bool ownsResources() const noexcept
    {
        return ownsResources_;
    }

private:
    void discardResources() noexcept;
    void fail(const std::exception& error);

    const Device& device_;
    VulkanRenderer& renderer_;
    RenderAssetCache& cache_;
    render::SceneResourceRequest request_;
    render::ScenePreparationStatus status_;
    bool ownsResources_ = false;
    VulkanUploadService& uploads_;
};
} // namespace rubia::rhi::vulkan
