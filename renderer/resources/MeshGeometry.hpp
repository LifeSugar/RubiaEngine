#pragma once
#include "asset/MeshAsset.hpp"
#include "rhi/Device.hpp"
#include "rhi/UploadService.hpp"
namespace rubia::render
{
class MeshGeometry final
{
  public:
    MeshGeometry() = default;
    MeshGeometry(const MeshGeometry &) = delete;
    MeshGeometry &operator=(const MeshGeometry &) = delete;
    MeshGeometry(MeshGeometry &&) noexcept = default;
    MeshGeometry &operator=(MeshGeometry &&) noexcept = default;
    void allocate(const rhi::Device &, const asset::MeshAsset &);
    static rhi::UploadRequest makeUploadRequest(std::shared_ptr<MeshGeometry>,
                                                std::shared_ptr<const asset::MeshAsset>);
    void reset() noexcept
    {
        submeshes_.clear();
        localBounds_ = {};
        indexBuffer_.reset();
        vertexBuffer_.reset();
    }
    const rhi::BufferRef &vertexBuffer() const noexcept { return vertexBuffer_; }
    const rhi::BufferRef &indexBuffer() const noexcept { return indexBuffer_; }
    const std::vector<asset::SubmeshData> &submeshes() const noexcept { return submeshes_; }
    const math::Aabb &localBounds() const noexcept { return localBounds_; }
    explicit operator bool() const noexcept { return vertexBuffer_ && !submeshes_.empty(); }

  private:
    rhi::BufferRef vertexBuffer_, indexBuffer_;
    std::vector<asset::SubmeshData> submeshes_;
    math::Aabb localBounds_;
};
} // namespace rubia::render
