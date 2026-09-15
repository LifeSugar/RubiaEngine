#pragma once

#include "asset/MeshAsset.hpp"
#include "vulkan/Buffer.hpp"
#include "vulkan/VulkanUploadTypes.hpp"

#include <vector>

namespace rubia::rhi::vulkan
{

/// Owns the GPU buffers and draw ranges of one mesh.
class Mesh final
{
public:
    /// Creates an empty GPU mesh.
    Mesh() = default;
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    /// Transfers mesh buffers and submesh metadata.
    Mesh(Mesh&&) noexcept = default;
    /// Replaces this mesh by taking another mesh's resources.
    Mesh& operator=(Mesh&&) noexcept = default;

    /// Allocates unpublished GPU buffers; upload completion is tracked separately.
    void allocate(const Device& device, const asset::MeshAsset& asset);
    static UploadRequest makeUploadRequest(std::shared_ptr<Mesh> mesh,
        std::shared_ptr<const asset::MeshAsset> source);
    void reset() noexcept;
    /// Binds the vertex and index buffers to a command buffer.
    void bind(VkCommandBuffer commandBuffer) const;

    /// Returns the uploaded vertex-buffer handle.
    [[nodiscard]] VkBuffer vertexBuffer() const noexcept { return vertexBuffer_.get(); }
    /// Returns the uploaded index-buffer handle.
    [[nodiscard]] VkBuffer indexBuffer() const noexcept { return indexBuffer_.get(); }
    /// Returns draw ranges for the uploaded mesh primitives.
    [[nodiscard]] const std::vector<asset::SubmeshData>& submeshes() const noexcept { return submeshes_; }
    /// Returns the CPU-computed bounds in mesh-local space.
    [[nodiscard]] const math::Aabb& localBounds() const noexcept { return localBounds_; }
    /// Returns whether the mesh contains usable GPU buffers and draw ranges.
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return vertexBuffer_ && !submeshes_.empty();
    }

private:
    /// Device-local storage for all mesh vertices.
    Buffer vertexBuffer_;
    /// Device-local storage for all mesh indices.
    Buffer indexBuffer_;
    /// Draw ranges corresponding to the source mesh primitives.
    std::vector<asset::SubmeshData> submeshes_;
    /// CPU metadata copied from the source MeshAsset for visibility tests.
    math::Aabb localBounds_;
};

} // namespace rubia::rhi::vulkan
