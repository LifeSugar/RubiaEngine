#include "vulkan/Mesh.hpp"

#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace rubia::rhi::vulkan
{
namespace
{

VkDeviceSize checkedBufferSize(
    std::size_t elementCount,
    std::size_t elementSize,
    const char* description)
{
    if (elementCount == 0 ||
        elementCount > std::numeric_limits<VkDeviceSize>::max() / elementSize)
    {
        throw std::overflow_error(std::string(description) + " has an invalid byte size");
    }
    return static_cast<VkDeviceSize>(elementCount * elementSize);
}

} // namespace

void Mesh::allocate(const Device& device, const asset::MeshAsset& asset)
{
    if (asset.empty())
    {
        throw std::invalid_argument("cannot allocate an empty mesh");
    }
    Mesh replacement;
    replacement.vertexBuffer_.create(
        device, checkedBufferSize(asset.vertices().size(), sizeof(asset::Vertex), "vertices"),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (!asset.indices().empty())
    {
        replacement.indexBuffer_.create(
            device, checkedBufferSize(asset.indices().size(), sizeof(uint32_t), "indices"),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    }
    replacement.submeshes_ = asset.submeshes();
    replacement.localBounds_ = asset.localBounds();
    *this = std::move(replacement);
}

UploadRequest Mesh::makeUploadRequest(std::shared_ptr<Mesh> mesh,
                                      std::shared_ptr<const asset::MeshAsset> source)
{
    if (!mesh || !*mesh || !source)
    {
        throw std::invalid_argument("missing mesh upload owner");
    }
    UploadRequest request;
    BufferUpload vertices;
    vertices.destination = std::shared_ptr<const Buffer>(mesh, &mesh->vertexBuffer_);
    vertices.source = {source, reinterpret_cast<const std::byte*>(source->vertices().data()),
                       source->vertices().size() * sizeof(asset::Vertex)};
    vertices.finalStages = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
    vertices.finalAccess = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    request.operations.emplace_back(std::move(vertices));
    if (!source->indices().empty())
    {
        BufferUpload indices;
        indices.destination = std::shared_ptr<const Buffer>(mesh, &mesh->indexBuffer_);
        indices.source = {source, reinterpret_cast<const std::byte*>(source->indices().data()),
                          source->indices().size() * sizeof(uint32_t)};
        indices.finalStages = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
        indices.finalAccess = VK_ACCESS_INDEX_READ_BIT;
        request.operations.emplace_back(std::move(indices));
    }
    return request;
}

void Mesh::reset() noexcept
{
    submeshes_.clear();
    localBounds_ = {};
    indexBuffer_.reset();
    vertexBuffer_.reset();
}

void Mesh::bind(VkCommandBuffer commandBuffer) const
{
    if (commandBuffer == VK_NULL_HANDLE || !*this)
    {
        throw std::invalid_argument("cannot bind an invalid Mesh or command buffer");
    }

    const VkBuffer vertexBuffer = vertexBuffer_.get();
    constexpr VkDeviceSize kOffset = 0;
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer, &kOffset);
    if (indexBuffer_)
    {
        vkCmdBindIndexBuffer(
            commandBuffer,
            indexBuffer_.get(),
            0,
            VK_INDEX_TYPE_UINT32);
    }
}

} // namespace rubia::rhi::vulkan
