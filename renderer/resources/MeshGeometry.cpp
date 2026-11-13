#include "resources/MeshGeometry.hpp"
#include <limits>
#include <stdexcept>
namespace rubia::render
{
namespace
{
uint64_t checkedSize(std::size_t count, std::size_t size)
{
    if (!count || count > std::numeric_limits<uint64_t>::max() / size)
        throw std::overflow_error("invalid mesh buffer size");
    return count * size;
}
} // namespace
void MeshGeometry::allocate(const rhi::Device &device, const asset::MeshAsset &source)
{
    if (source.empty())
        throw std::invalid_argument("cannot allocate empty mesh");
    MeshGeometry replacement;
    replacement.vertexBuffer_ = device.createBuffer(
        {checkedSize(source.vertices().size(), sizeof(asset::Vertex)),
         rhi::flag(rhi::BufferUsage::Vertex) | rhi::flag(rhi::BufferUsage::TransferDestination),
         rhi::flag(rhi::MemoryProperty::DeviceLocal)});
    if (!source.indices().empty())
        replacement.indexBuffer_ = device.createBuffer(
            {checkedSize(source.indices().size(), sizeof(uint32_t)),
             rhi::flag(rhi::BufferUsage::Index) | rhi::flag(rhi::BufferUsage::TransferDestination),
             rhi::flag(rhi::MemoryProperty::DeviceLocal)});
    replacement.submeshes_ = source.submeshes();
    replacement.localBounds_ = source.localBounds();
    *this = std::move(replacement);
}
rhi::UploadRequest MeshGeometry::makeUploadRequest(std::shared_ptr<MeshGeometry> mesh,
                                                   std::shared_ptr<const asset::MeshAsset> source)
{
    if (!mesh || !*mesh || !source)
        throw std::invalid_argument("missing mesh upload owner");
    rhi::UploadRequest request;
    request.operations.emplace_back(
        rhi::BufferUpload{mesh->vertexBuffer_,
                          0,
                          {source, reinterpret_cast<const std::byte *>(source->vertices().data()),
                           source->vertices().size() * sizeof(asset::Vertex)},
                          {rhi::AccessUsage::VertexRead, 0}});
    if (!source->indices().empty())
        request.operations.emplace_back(rhi::BufferUpload{
            mesh->indexBuffer_,
            0,
            {source, reinterpret_cast<const std::byte *>(source->indices().data()),
             source->indices().size() * sizeof(uint32_t)},
            {rhi::AccessUsage::IndexRead, 0}});
    return request;
}
} // namespace rubia::render
