#pragma once

#include "asset/AssetManager.hpp"
#include "vulkan/DescriptorPool.hpp"
#include "vulkan/DescriptorSetLayout.hpp"
#include "vulkan/GpuMaterial.hpp"
#include "vulkan/GpuTexture.hpp"
#include "vulkan/Mesh.hpp"
#include "vulkan/VulkanUploadService.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace rubia::rhi::vulkan
{

/// Renderer-owned GPU representations reachable from one or more ModelAssets.
class RenderAssetCache final
{
public:
    RenderAssetCache() = default;
    ~RenderAssetCache();

    RenderAssetCache(const RenderAssetCache&) = delete;
    RenderAssetCache& operator=(const RenderAssetCache&) = delete;
    RenderAssetCache(RenderAssetCache&&) = delete;
    RenderAssetCache& operator=(RenderAssetCache&&) = delete;

    void create(
        const Device& device,
        UploadContext& uploadContext,
        const asset::AssetManager& assets,
        const std::vector<asset::ModelAssetHandle>& models);
    /// Establishes the material interface without requiring a model instance.
    void initialize(const Device& device, const asset::MaterialTemplateAsset& materialTemplate);
    /// Allocates cache slots and descriptors; leaves resource uploads to uploadNext.
    void beginUpload(const Device& device, const asset::AssetManager& assets,
        const std::vector<asset::ModelAssetHandle>& models);
    /// Prepares one texture, material or mesh. An explicit UploadContext batch
    /// lets the caller submit it and poll completion between GUI frames.
    void uploadNext(const Device& device, UploadContext& uploadContext,
        const asset::AssetManager& assets);
    [[nodiscard]] std::size_t pendingUploadCount() const noexcept;
    // Scene preparation adapter. Publishes only fence-completed textures/meshes.
    void prepareNext(const Device& device, VulkanUploadService& uploads,
        std::shared_ptr<const asset::AssetManager> assets);
    bool hasSubmittedUpload(const VulkanUploadService& uploads) const;
    void cancelPendingUpload(VulkanUploadService& uploads);
    void publishTexture(asset::TextureAssetHandle handle, GpuTexture texture);
    bool empty() const noexcept;

    /// Builds a replacement without changing the live cache. This lets the
    /// caller finish disk/CPU validation before committing the GPU swap.
    [[nodiscard]] GpuTexture stageTextureReplacement(
        const Device& device,
        UploadContext& uploadContext,
        const asset::TextureAsset& replacement) const;
    /// Commits a staged texture under the existing handle and rewrites every
    /// cached material descriptor that references it. The caller must ensure
    /// no submitted frame is using the old descriptors/resources.
    [[nodiscard]] GpuTexture commitTextureReplacement(
        const Device& device,
        const asset::AssetManager& assets,
        asset::TextureAssetHandle handle,
        GpuTexture replacement);
    void reset() noexcept;

    [[nodiscard]] const Mesh& mesh(asset::MeshAssetHandle handle) const;
    [[nodiscard]] const Mesh* tryMesh(
        asset::MeshAssetHandle handle) const noexcept;
    [[nodiscard]] const GpuMaterial& material(
        asset::MaterialAssetHandle handle) const;
    [[nodiscard]] const GpuMaterial* tryMaterial(
        asset::MaterialAssetHandle handle) const noexcept;
    /// Returns an uploaded texture for Editor previews and material binding.
    [[nodiscard]] const GpuTexture& texture(
        asset::TextureAssetHandle handle) const;
    [[nodiscard]] const GpuTexture* tryTexture(
        asset::TextureAssetHandle handle) const noexcept;
    [[nodiscard]] VkDescriptorSetLayout materialDescriptorSetLayout() const
        noexcept
    {
        return materialDescriptorSetLayout_.get();
    }
private:
    struct PendingUpload
    {
        std::shared_ptr<GpuTexture> texture;
        std::shared_ptr<Mesh> mesh;
        UploadRequest request;
        UploadTicket ticket;
    } pendingUpload_;

    struct TextureEntry
    {
        uint32_t generation = 0;
        GpuTexture texture;
    };

    struct MaterialEntry
    {
        uint32_t generation = 0;
        GpuMaterial material;
    };

    struct MeshEntry
    {
        uint32_t generation = 0;
        Mesh mesh;
    };

    // Pool is declared last so it destroys descriptor sets before their
    // referenced buffers, image views, and samplers.
    DescriptorSetLayout materialDescriptorSetLayout_;
    std::vector<TextureEntry> textures_;
    std::vector<MaterialEntry> materials_;
    std::vector<MeshEntry> meshes_;
    DescriptorPool materialDescriptorPool_;
    std::vector<asset::TextureAssetHandle> pendingTextures_;
    std::vector<asset::MaterialAssetHandle> pendingMaterials_;
    std::vector<asset::MeshAssetHandle> pendingMeshes_;
    std::vector<VkDescriptorSet> uploadMaterialSets_;
    std::size_t uploadedTextures_ = 0;
    std::size_t uploadedMaterials_ = 0;
    std::size_t uploadedMeshes_ = 0;
};

} // namespace rubia::rhi::vulkan
