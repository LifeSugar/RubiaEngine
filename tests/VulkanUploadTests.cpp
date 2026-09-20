#include "VulkanUploadTests.hpp"
#include "vulkan/Fence.hpp"
#include "vulkan/RenderAssetCache.hpp"
#include "vulkan/VulkanResourcePreparation.hpp"
#include "vulkan/VulkanUploadService.hpp"
#include <cstring>
#include <stdexcept>
#include <thread>

namespace rubia::test
{
using namespace rhi::vulkan;
namespace
{
void require(bool value, const char* message)
{
    if (!value)
    {
        throw std::runtime_error(message);
    }
}
std::shared_ptr<Buffer> destination(const Device& device, uint64_t bytes = 64)
{
    return std::make_shared<Buffer>(
        device, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
}
UploadRequest request(std::shared_ptr<Buffer> target,
                      std::shared_ptr<std::vector<std::byte>> source, VkDeviceSize offset = 0)
{
    BufferUpload op;
    op.destination = target;
    op.destinationOffset = offset;
    op.source = {source, source->data(), source->size()};
    op.finalStages = VK_PIPELINE_STAGE_TRANSFER_BIT;
    op.finalAccess = VK_ACCESS_TRANSFER_READ_BIT;
    return {{op}};
}
std::vector<std::byte> readBack(const Device& device, const Buffer& target, uint64_t offset,
                                uint64_t bytes)
{
    Buffer host(device, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    CommandPool pool(device, device.graphicsQueueFamily());
    auto cb = pool.allocatePrimary();
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    require(vkBeginCommandBuffer(cb, &begin) == VK_SUCCESS, "readback begin failed");
    VkBufferCopy region{offset, 0, bytes};
    vkCmdCopyBuffer(cb, target.get(), host.get(), 1, &region);
    VkBufferMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = host.get();
    barrier.size = bytes;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0,
                         nullptr, 1, &barrier, 0, nullptr);
    require(vkEndCommandBuffer(cb) == VK_SUCCESS, "readback end failed");
    Fence fence(device.get());
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cb;
    require(vkQueueSubmit(device.graphicsQueue(), 1, &submit, fence.get()) == VK_SUCCESS,
            "readback submit failed");
    fence.wait();
    std::vector<std::byte> result(static_cast<std::size_t>(bytes));
    std::memcpy(result.data(), host.map(), result.size());
    return result;
}
// The caller has arranged TRANSFER_SRC access before readback.
std::vector<std::byte> readBackImage(const Device& device, const Image& target,
                                     const std::vector<VkBufferImageCopy>& regions, size_t bytes)
{
    Buffer host(device, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    CommandPool pool(device, device.graphicsQueueFamily());
    auto cb = pool.allocatePrimary();
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    require(vkBeginCommandBuffer(cb, &begin) == VK_SUCCESS, "image readback begin failed");
    vkCmdCopyImageToBuffer(cb, target.get(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, host.get(),
                           static_cast<uint32_t>(regions.size()), regions.data());
    VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = host.get();
    barrier.size = bytes;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0,
                         nullptr, 1, &barrier, 0, nullptr);
    require(vkEndCommandBuffer(cb) == VK_SUCCESS, "image readback end failed");
    Fence fence(device.get());
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cb;
    require(vkQueueSubmit(device.graphicsQueue(), 1, &submit, fence.get()) == VK_SUCCESS,
            "image readback submit failed");
    fence.wait();
    std::vector<std::byte> result(bytes);
    std::memcpy(result.data(), host.map(), bytes);
    return result;
}
void runImageUpdateTests(const Device& device)
{
    VulkanUploadService uploads(device);
    const auto complete = [&](ImageUpload op)
    {
        UploadRequest request{{std::move(op)}};
        const auto result = uploads.tryEnqueue(request);
        if (!result.accepted())
        {
            throw std::runtime_error(result.error);
        }
        uploads.drain();
        require(uploads.query(result.ticket).state == UploadState::Completed,
                "image update failed");
        uploads.releaseTicket(result.ticket);
    };
    Image::CreateInfo desc;
    desc.extent = {4, 4, 1};
    desc.mipLevels = 2;
    desc.arrayLayers = 2;
    desc.format = VK_FORMAT_R8G8B8A8_UNORM;
    desc.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    desc.memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    auto image = std::make_shared<Image>(device, desc);
    auto original = std::make_shared<std::vector<std::byte>>(160);
    for (size_t i = 0; i < original->size(); ++i)
    {
        (*original)[i] = std::byte(i);
    }
    ImageUpload init;
    init.destination = image;
    init.source = {original, original->data(), original->size()};
    init.range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 2, 0, 2};
    init.before = {VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0};
    init.after = {VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_ACCESS_TRANSFER_READ_BIT};
    VkBufferImageCopy mip0{};
    mip0.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 2};
    mip0.imageExtent = {4, 4, 1};
    VkBufferImageCopy mip1 = mip0;
    mip1.bufferOffset = 128;
    mip1.imageSubresource.mipLevel = 1;
    mip1.imageExtent = {2, 2, 1};
    init.regions = {mip0, mip1};
    complete(init);
    require(readBackImage(device, *image, init.regions, 160) == *original,
            "initial array/mip image upload mismatch");

    // Two disjoint rectangles in the same mip, two layers, padded rows and layer stride.
    // The barrier range includes mip 1, although no copies update that mip.
    auto payload = std::make_shared<std::vector<std::byte>>(80);
    for (size_t i = 0; i < payload->size(); ++i)
    {
        (*payload)[i] = std::byte(255 - i);
    }
    ImageUpload patch = init;
    patch.before = init.after;
    patch.source = {payload, payload->data(), payload->size()};
    VkBufferImageCopy rectangle{};
    rectangle.bufferOffset = 4;
    rectangle.bufferRowLength = 4;
    rectangle.bufferImageHeight = 3;
    rectangle.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 2};
    rectangle.imageOffset = {1, 1, 0};
    rectangle.imageExtent = {2, 2, 1};
    VkBufferImageCopy corner{};
    corner.bufferOffset = 76;
    corner.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 1};
    corner.imageExtent = {1, 1, 1};
    patch.regions = {rectangle, corner};
    const auto reject = [&](ImageUpload op, UploadEnqueueCode code, const char* message)
    {
        UploadRequest request{{std::move(op)}};
        require(uploads.tryEnqueue(request).code == code && request.operations.size() == 1,
                message);
    };
    auto bad = patch;
    bad.regions.resize(1);
    bad.source.size = 75; // Last copied byte is 75; size must be at least 76.
    reject(bad, UploadEnqueueCode::InvalidRequest, "padded multilayer source overrun accepted");
    bad = patch;
    bad.regions[0].bufferRowLength = 1;
    reject(bad, UploadEnqueueCode::InvalidRequest, "short row pitch accepted");
    bad = patch;
    bad.regions[0].imageOffset.x = 3;
    reject(bad, UploadEnqueueCode::InvalidRequest, "rectangle beyond image accepted");
    bad = patch;
    bad.regions[0].bufferOffset = UINT64_MAX - 3;
    reject(bad, UploadEnqueueCode::InvalidRequest, "source offset overflow accepted");
    bad = patch;
    bad.before = {};
    reject(bad, UploadEnqueueCode::InvalidRequest, "implicit prior state accepted");
    bad = patch;
    bad.after.layout = VK_IMAGE_LAYOUT_UNDEFINED;
    reject(bad, UploadEnqueueCode::InvalidRequest, "undefined final layout accepted");
    bad = patch;
    bad.before.stages = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    reject(bad, UploadEnqueueCode::InvalidRequest, "incompatible access/stage accepted");
    bad = patch;
    bad.source.owner.reset();
    reject(bad, UploadEnqueueCode::InvalidRequest, "unowned queued bytes accepted");
    bad = patch;
    bad.after.layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    reject(bad, UploadEnqueueCode::UnsupportedRequest, "unsupported boundary layout misclassified");
    complete(patch);
    auto expected = *original;
    for (size_t layer = 0; layer < 2; ++layer)
    {
        for (size_t y = 1; y < 3; ++y)
        {
            for (size_t x = 1; x < 3; ++x)
            {
                for (size_t channel = 0; channel < 4; ++channel)
                {
                    expected[layer * 64 + (y * 4 + x) * 4 + channel] =
                        (*payload)[4 + layer * 48 + (y - 1) * 16 + (x - 1) * 4 + channel];
                }
            }
        }
    }
    for (size_t c = 0; c < 4; ++c)
    {
        expected[64 + c] = (*payload)[76 + c];
    }
    require(readBackImage(device, *image, init.regions, 160) == expected,
            "partial update changed untouched pixels/mips or copied wrong row/layer stride");

    // A subset of the barrier range is also valid. No final-row padding is required.
    patch.range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 2};
    patch.regions.resize(1);
    patch.source.size = 76;
    complete(patch);
    require(readBackImage(device, *image, init.regions, 160) == expected,
            "subset range update did not preserve other mip levels");

    // On this graphics queue, R8 copies require texel-block alignment, not 4-byte alignment.
    desc.extent = {2, 2, 1};
    desc.mipLevels = desc.arrayLayers = 1;
    desc.format = VK_FORMAT_R8_UNORM;
    auto r8 = std::make_shared<Image>(device, desc);
    auto r8Bytes = std::make_shared<std::vector<std::byte>>(5, std::byte{0x42});
    ImageUpload r8Op;
    r8Op.destination = r8;
    r8Op.source = {r8Bytes, r8Bytes->data(), r8Bytes->size()};
    r8Op.before = init.before;
    r8Op.after = init.after;
    r8Op.range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkBufferImageCopy r8Region{};
    r8Region.bufferOffset = 1;
    r8Region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    r8Region.imageExtent = {2, 2, 1};
    r8Op.regions = {r8Region};
    complete(r8Op);
    r8Region.bufferOffset = 0;
    require(readBackImage(device, *r8, {r8Region}, 4) == std::vector<std::byte>(4, std::byte{0x42}),
            "R8 byte-aligned copy failed");

    // Compressed edge extents may be smaller than a block; interior offsets still align.
    desc.extent = {6, 6, 1};
    desc.format = VK_FORMAT_BC7_UNORM_BLOCK;
    auto bc = std::make_shared<Image>(device, desc);
    auto bcBytes = std::make_shared<std::vector<std::byte>>(64, std::byte{0});
    ImageUpload bcOp = r8Op;
    bcOp.destination = bc;
    bcOp.source = {bcBytes, bcBytes->data(), bcBytes->size()};
    bcOp.regions[0].bufferOffset = 0;
    bcOp.regions[0].imageExtent = {6, 6, 1};
    complete(bcOp);
    const auto bcReadRegions = bcOp.regions;
    auto block = std::make_shared<std::vector<std::byte>>(16, std::byte{0x40});
    bcOp.before = bcOp.after;
    bcOp.source = {block, block->data(), block->size()};
    bcOp.regions[0].imageOffset = {4, 4, 0};
    bcOp.regions[0].imageExtent = {2, 2, 1};
    bad = bcOp;
    bad.regions[0].imageOffset.x = 2;
    reject(bad, UploadEnqueueCode::InvalidRequest, "misaligned compressed offset accepted");
    bad = bcOp;
    bad.regions[0].bufferOffset = 4;
    reject(bad, UploadEnqueueCode::InvalidRequest, "misaligned compressed source accepted");
    complete(bcOp);
    auto bcExpected = *bcBytes;
    std::memcpy(bcExpected.data() + 48, block->data(), block->size());
    require(readBackImage(device, *bc, bcReadRegions, 64) == bcExpected,
            "compressed edge update changed other blocks");

    desc.type = VK_IMAGE_TYPE_3D;
    desc.extent = {2, 2, 2};
    desc.format = VK_FORMAT_R8G8B8A8_UNORM;
    bad = r8Op;
    bad.destination = std::make_shared<Image>(device, desc);
    reject(bad, UploadEnqueueCode::UnsupportedRequest, "3D service limitation misclassified");
}
std::shared_ptr<asset::MeshAsset> preparationMesh()
{
    asset::MeshAsset::CreateInfo info;
    info.vertices.resize(3);
    info.indices = {0, 1, 2};
    info.submeshes.push_back({0, 3, 0, 3, {}});
    return std::make_shared<asset::MeshAsset>(std::move(info));
}
void runMeshPreparationTests(const Device& device)
{
    VulkanUploadService uploads(device);
    RenderAssetCache cache;
    VulkanResourcePreparation preparation(device, uploads);
    const auto domain = std::make_shared<asset::AssetDomainTag>();
    const asset::MeshAssetHandle meshHandle{0, 1};
    auto source = preparationMesh();
    const auto mesh = preparation.prepareMesh(cache, {domain, {meshHandle, 1}, source});
    require(mesh.accepted(), "standalone mesh preparation was rejected");
    auto shared = preparation.prepareMesh(cache, {domain, {meshHandle, 1}, source});
    require(shared.accepted() && shared.disposition == ResourcePreparationDisposition::Shared,
            "duplicate preparation was not shared");
    preparation.cancel(shared.ticket);
    preparation.release(shared.ticket);
    // Type is part of the target: identical texture/mesh slot numbers must coexist.
    asset::TextureAsset::CreateInfo image;
    image.width = image.height = 2;
    image.format = asset::TextureFormat::RGBA8UNorm;
    image.payload.assign(16, std::byte{0x42});
    const asset::TextureAssetHandle textureHandle{0, 1};
    const auto texture = preparation.prepareTexture(
        cache,
        {domain, {textureHandle, 1}, std::make_shared<asset::TextureAsset>(std::move(image))});
    require(texture.accepted(), "texture and mesh slot identities collided");
    bool releaseRejected = false;
    try
    {
        preparation.release(mesh.ticket);
    }
    catch (const std::logic_error&)
    {
        releaseRejected = true;
    }
    require(releaseRejected, "unfinished preparation could be released");

    // One indexed mesh has vertex and index operations; finish only the first batch.
    uploads.tick({4096, 1, std::chrono::seconds(1)});
    device.waitIdle();
    uploads.tick({0, 0, std::chrono::microseconds{0}});
    preparation.advance();
    const auto partial = preparation.status(mesh.ticket);
    require(partial.state == ResourcePreparationState::Uploading && partial.completedBytes > 0 &&
                partial.completedBytes < partial.totalBytes && !cache.tryMesh(meshHandle),
            "partial mesh upload was published");
    uploads.drain();
    require(preparation.status(mesh.ticket).state == ResourcePreparationState::Uploading &&
                !cache.tryMesh(meshHandle),
            "transfer completion published a mesh without advance");
    preparation.advance();
    const auto complete = preparation.status(mesh.ticket);
    require(complete.state == ResourcePreparationState::Ready &&
                complete.completedBytes == complete.totalBytes &&
                complete.submittedBytes == complete.totalBytes && cache.tryMesh(meshHandle) &&
                cache.mesh(meshHandle).indexBuffer() &&
                cache.mesh(meshHandle).submeshes().front().indexCount == 3 &&
                preparation.status(texture.ticket).state == ResourcePreparationState::Ready &&
                cache.tryTexture(textureHandle),
            "common preparation did not publish both asset types");
    preparation.release(mesh.ticket);
    preparation.release(texture.ticket);
    require(preparation.empty() && cache.tryMesh(meshHandle),
            "ticket release removed published mesh");

    // Cancellation after one submission must retain the source until that batch retires.
    const asset::MeshAssetHandle cancelledHandle{1, 1};
    auto cancelledSource = preparationMesh();
    std::weak_ptr<asset::MeshAsset> weak = cancelledSource;
    const auto cancelled =
        preparation.prepareMesh(cache, {domain, {cancelledHandle, 1}, cancelledSource});
    require(cancelled.accepted(), "cancelled mesh test enqueue failed");
    cancelledSource.reset();
    uploads.tick({4096, 1, std::chrono::seconds(1)});
    preparation.cancel(cancelled.ticket);
    require(preparation.status(cancelled.ticket).state == ResourcePreparationState::Cancelled &&
                !weak.expired() && !cache.tryMesh(cancelledHandle),
            "mesh cancellation released in-flight resources or published a partial mesh");
    preparation.release(cancelled.ticket);
    uploads.drain();
    preparation.advance();
    require(weak.expired() && !cache.tryMesh(cancelledHandle),
            "cancelled mesh leaked or was published");
}

struct PreparationProbe
{
    int created = 0;
    int built = 0;
    int published = 0;
    int destroyed = 0;
    bool failCreate = false;
    bool failPublish = false;
};
class NoUploadPreparation final : public IResourcePreparation
{
public:
    explicit NoUploadPreparation(PreparationProbe& probe) : probe_(probe)
    {
    }
    ~NoUploadPreparation() override
    {
        ++probe_.destroyed;
    }
    void createGpuResources(const Device&) override
    {
        ++probe_.created;
        if (probe_.failCreate)
        {
            throw std::runtime_error("probe create failed");
        }
    }
    UploadRequest buildUploadRequest() override
    {
        ++probe_.built;
        return {};
    }
    void publish() override
    {
        if (probe_.failPublish)
        {
            throw std::runtime_error("probe publish failed");
        }
        ++probe_.published;
    }

private:
    PreparationProbe& probe_;
};
void runNoUploadPreparationTests(const Device& device)
{
    // Occupy the only upload ticket. Empty-transfer preparations must still work.
    VulkanUploadService uploads(device, {1024, 2048, 1});
    auto bytes = std::make_shared<std::vector<std::byte>>(16, std::byte{0x31});
    auto pending = request(destination(device), bytes);
    const auto occupied = uploads.tryEnqueue(pending);
    require(occupied.accepted(), "no-upload test could not occupy service capacity");
    RenderAssetCache cache;
    PreparationProbe ready, cancelled, failed, retry, rejected, createFailed;
    VulkanResourcePreparation preparation(device, uploads, 2);
    const auto domain = std::make_shared<asset::AssetDomainTag>();
    const auto enqueue = [&](uint32_t index, PreparationProbe& probe)
    {
        return preparation.prepare({&cache, asset::MeshAssetHandle{index, 1}, 1, domain},
                                   std::make_unique<NoUploadPreparation>(probe));
    };
    auto a = enqueue(0, ready);
    auto b = enqueue(1, cancelled);
    require(a.accepted() && b.accepted() && ready.created == 1 && ready.built == 1 &&
                ready.published == 0 && preparation.status(a.ticket).totalBytes == 0,
            "empty-transfer preparation required upload capacity or published early");
    require(enqueue(2, rejected).code == ResourcePreparationCode::QueueFull &&
                rejected.created == 0 && rejected.destroyed == 1,
            "no-upload preparation records were not bounded before creation");
    bool foreignThreadRejected = false;
    std::thread foreign(
        [&]
        {
            try
            {
                preparation.advance();
            }
            catch (const std::logic_error&)
            {
                foreignThreadRejected = true;
            }
        });
    foreign.join();
    require(foreignThreadRejected, "no-upload preparation bypassed render-thread ownership");
    preparation.cancel(b.ticket);
    preparation.advance();
    preparation.advance();
    require(preparation.status(a.ticket).state == ResourcePreparationState::Ready &&
                ready.published == 1 && ready.destroyed == 1 &&
                preparation.status(b.ticket).state == ResourcePreparationState::Cancelled &&
                cancelled.published == 0 && cancelled.destroyed == 1,
            "no-upload completion/cancellation failed or published more than once");
    preparation.release(a.ticket);
    preparation.release(b.ticket);
    failed.failPublish = true;
    a = enqueue(0, failed);
    b = enqueue(1, retry);
    require(a.accepted() && b.accepted(), "terminal records did not release preparation capacity");
    preparation.advance();
    require(preparation.status(a.ticket).state == ResourcePreparationState::Failed &&
                preparation.status(a.ticket).error == "probe publish failed" &&
                failed.destroyed == 1 &&
                preparation.status(b.ticket).state == ResourcePreparationState::Ready,
            "publication failure was lost or damaged another preparation");
    preparation.release(a.ticket);
    preparation.release(b.ticket);
    createFailed.failCreate = true;
    require(enqueue(0, createFailed).code == ResourcePreparationCode::Failed &&
                createFailed.destroyed == 1 && preparation.empty(),
            "creation failure leaked a record");
    require(uploads.query(occupied.ticket).state == UploadState::Queued &&
                uploads.stagedBytes() == 0,
            "preparation advanced unrelated service uploads");
    uploads.cancel(occupied.ticket);
    uploads.releaseTicket(occupied.ticket);
}
void runVersionedPreparationTests(const Device& device)
{
    // One transfer record proves that duplicate preparations do not enqueue a second upload.
    VulkanUploadService uploads(device, {1024, 2048, 1});
    RenderAssetCache cache;
    PreparationProbe failed;
    VulkanResourcePreparation preparation(device, uploads);
    asset::AssetManager assets;
    asset::TextureAsset::CreateInfo info;
    info.width = info.height = 2;
    info.format = asset::TextureFormat::RGBA8UNorm;
    info.payload.assign(16, std::byte{0x41});
    const auto handle = assets.createTexture(info);
    const auto v1 = assets.snapshot(handle);
    const auto target = [&](const auto& snapshot)
    {
        return ResourcePreparationTarget{&cache, snapshot.version.handle,
                                         snapshot.version.contentRevision, snapshot.domain};
    };
    auto a = preparation.prepareTexture(cache, v1);
    auto b = preparation.prepareTexture(cache, v1);
    require(a.accepted() && b.accepted() && a.ticket.value != b.ticket.value &&
                b.disposition == ResourcePreparationDisposition::Shared,
            "identical texture versions did not share one upload with separate caller tickets");
    preparation.cancel(a.ticket);
    preparation.release(a.ticket);
    uploads.drain();
    // Sharing still works after transfer completion but before publication.
    auto c = preparation.prepareTexture(cache, v1);
    require(c.accepted() && c.disposition == ResourcePreparationDisposition::Shared,
            "completed but unpublished preparation was not shared");
    preparation.advance();
    require(preparation.status(b.ticket).state == ResourcePreparationState::Ready &&
                preparation.status(c.ticket).state == ResourcePreparationState::Ready,
            "one subscriber cancellation damaged the shared task");
    const auto originalView = cache.texture(handle).view();
    preparation.release(b.ticket);
    preparation.release(c.ticket);
    auto hit = preparation.prepareTexture(cache, v1);
    require(hit.accepted() && hit.disposition == ResourcePreparationDisposition::CacheHit &&
                preparation.status(hit.ticket).state == ResourcePreparationState::Ready &&
                cache.texture(handle).view() == originalView && uploads.stagedBytes() == 0,
            "cache hit allocated/uploaded another texture or was not immediately ready");
    preparation.release(hit.ticket);

    info.payload.assign(16, std::byte{0x52});
    static_cast<void>(assets.replaceTexture(handle, asset::TextureAsset(info)));
    const auto v2 = assets.snapshot(handle);
    a = preparation.prepareTexture(cache, v2);
    b = preparation.prepareTexture(cache, v2);
    require(a.accepted() && b.accepted(), "updated version was rejected");
    uploads.tick({1024, 1, std::chrono::seconds(1)});
    preparation.cancel(a.ticket);
    preparation.cancel(b.ticket);
    preparation.release(a.ticket);
    preparation.release(b.ticket);
    uploads.drain();
    preparation.advance();
    require(cache.texture(handle).view() == originalView &&
                cache.inspectPreparation(target(v2)).resident == 1,
            "cancelling all update subscribers destroyed the resident version");

    a = preparation.prepareTexture(cache, v2);
    require(a.accepted(), "cancelled version could not be retried");
    uploads.drain();
    // No free record until the completed upload is published; rejection must preserve it.
    info.payload.assign(16, std::byte{0x63});
    static_cast<void>(assets.replaceTexture(handle, asset::TextureAsset(info)));
    const auto v3 = assets.snapshot(handle);
    auto rejected = preparation.prepareTexture(cache, v3);
    require(rejected.code == ResourcePreparationCode::QueueFull &&
                preparation.status(a.ticket).state == ResourcePreparationState::Uploading &&
                cache.inspectPreparation(target(v2)).requested == 2,
            "rejected newer version superseded accepted work");
    preparation.advance();
    require(preparation.status(a.ticket).state == ResourcePreparationState::Ready &&
                cache.texture(handle).view() != originalView &&
                cache.inspectPreparation(target(v2)).resident == 2,
            "texture replacement did not publish the requested revision");
    preparation.release(a.ticket);
    const auto secondView = cache.texture(handle).view();

    // Publication failure retains the old cache entry and can be retried.
    failed.failPublish = true;
    a = preparation.prepare(target(v3), std::make_unique<NoUploadPreparation>(failed));
    require(a.accepted(), "failure probe was not accepted");
    preparation.advance();
    require(preparation.status(a.ticket).state == ResourcePreparationState::Failed &&
                cache.texture(handle).view() == secondView &&
                cache.inspectPreparation(target(v3)).resident == 2,
            "failed publication lost the previous texture/version");
    preparation.release(a.ticket);
    a = preparation.prepareTexture(cache, v3);
    require(a.accepted(), "failed version could not be retried");
    uploads.drain();
    preparation.advance();
    preparation.release(a.ticket);
    require(cache.inspectPreparation(target(v3)).resident == 3 &&
                !preparation.prepareTexture(cache, v2).accepted(),
            "older version was allowed to overwrite newer resident data");

    asset::AssetManager other;
    const auto otherHandle = other.createTexture(info);
    require(otherHandle == handle &&
                !preparation.prepareTexture(cache, other.snapshot(otherHandle)).accepted(),
            "identical handles from different asset domains collided");
    auto recycled = v3;
    ++recycled.version.handle.generation;
    require(!preparation.prepareTexture(cache, recycled).accepted(),
            "another generation was silently treated as the same resource");

    // A second service with room for concurrent versions exercises supersession in flight.
    VulkanUploadService concurrent(device);
    VulkanResourcePreparation updates(device, concurrent);
    static_cast<void>(assets.replaceTexture(handle, asset::TextureAsset(info)));
    const auto v4 = assets.snapshot(handle);
    a = updates.prepareTexture(cache, v4);
    b = updates.prepareTexture(cache, v4);
    require(a.accepted() && b.accepted(), "supersession setup failed");
    concurrent.tick({1024, 1, std::chrono::seconds(1)});
    static_cast<void>(assets.replaceTexture(handle, asset::TextureAsset(info)));
    const auto v5 = assets.snapshot(handle);
    c = updates.prepareTexture(cache, v5);
    require(c.accepted() && updates.status(a.ticket).state == ResourcePreparationState::Cancelled &&
                updates.status(b.ticket).state == ResourcePreparationState::Cancelled,
            "new revision failed to supersede all observers of older pending work");
    concurrent.drain();
    updates.advance();
    require(updates.status(c.ticket).state == ResourcePreparationState::Ready &&
                cache.inspectPreparation(target(v5)).resident == 5,
            "older in-flight completion overwrote the latest texture");
    updates.release(a.ticket);
    updates.release(b.ticket);
    updates.release(c.ticket);

    // The same policy applies to Mesh, including unchanged buffer identity on cache hit.
    asset::MeshAsset::CreateInfo meshInfo;
    meshInfo.vertices.resize(3);
    meshInfo.indices = {0, 1, 2};
    meshInfo.submeshes.push_back({0, 3, 0, 3, {}});
    const auto meshHandle = assets.createMesh(meshInfo);
    a = updates.prepareMesh(cache, assets.snapshot(meshHandle));
    b = updates.prepareMesh(cache, assets.snapshot(meshHandle));
    require(a.accepted() && b.disposition == ResourcePreparationDisposition::Shared,
            "mesh preparation did not share");
    concurrent.drain();
    updates.advance();
    const auto originalBuffer = cache.mesh(meshHandle).vertexBuffer();
    updates.release(a.ticket);
    updates.release(b.ticket);
    a = updates.prepareMesh(cache, assets.snapshot(meshHandle));
    require(a.disposition == ResourcePreparationDisposition::CacheHit &&
                cache.mesh(meshHandle).vertexBuffer() == originalBuffer,
            "mesh cache miss");
    updates.release(a.ticket);
    meshInfo.vertices[0].position.x = 2.0f;
    static_cast<void>(assets.replaceMesh(meshHandle, asset::MeshAsset(meshInfo)));
    const auto meshV2 = assets.snapshot(meshHandle);
    a = updates.prepareMesh(cache, meshV2);
    require(a.accepted() && cache.mesh(meshHandle).vertexBuffer() == originalBuffer,
            "mesh update replaced the old buffer before completion");
    concurrent.drain();
    updates.advance();
    require(updates.status(a.ticket).state == ResourcePreparationState::Ready &&
                cache.mesh(meshHandle).vertexBuffer() != originalBuffer &&
                cache.inspectPreparation(target(meshV2)).resident == 2,
            "mesh update did not commit the new revision");
    updates.release(a.ticket);
}

void runPreparationLifetimeTests(const Device& device)
{
    // Destroying a preparation component must not shut down the shared service,
    // publish abandoned textures, or free resources that the GPU still uses.
    for (const bool submitted : {false, true})
    {
        VulkanUploadService uploads(device, {1024, 2048, 2});
        RenderAssetCache cache;
        auto bytes = std::make_shared<std::vector<std::byte>>(16, std::byte{0x31});
        auto survivorRequest = request(destination(device), bytes);
        const auto survivor = uploads.tryEnqueue(survivorRequest);
        require(survivor.accepted(), "shared service test enqueue failed");
        ResourcePreparationTicket textureTicket;
        const asset::TextureAssetHandle handle{0, 1};
        std::weak_ptr<asset::TextureAsset> weakSource;
        {
            VulkanResourcePreparation preparation(device, uploads);
            asset::TextureAsset::CreateInfo info;
            info.width = info.height = 2;
            info.format = asset::TextureFormat::RGBA8UNorm;
            info.payload.assign(16, std::byte{0x42});
            auto source = std::make_shared<asset::TextureAsset>(std::move(info));
            weakSource = source;
            const auto result = preparation.prepareTexture(
                cache, {std::make_shared<asset::AssetDomainTag>(), {handle, 1}, source});
            require(result.accepted(), "preparation lifetime test enqueue failed");
            textureTicket = result.ticket;
            source.reset();
            preparation.advance();
            require(preparation.status(textureTicket).state ==
                            ResourcePreparationState::Preparing &&
                        uploads.stagedBytes() == 0 && !cache.tryTexture(handle),
                    "preparation advance submitted or prematurely published work");
            if (submitted)
            {
                uploads.tick({64, 2, std::chrono::seconds(1)});
                require(preparation.status(textureTicket).submittedBytes == 16,
                        "texture did not enter the shared batch");
            }
        }
        require(cache.empty() && !cache.tryTexture(handle) && weakSource.expired() == !submitted,
                "preparation destruction published or incorrectly retained/released source");
        // The destroyed component must release its service record, leaving room for another
        // request.
        auto retry = request(destination(device), bytes);
        auto retried = uploads.tryEnqueue(retry);
        require(retried.accepted(), "preparation destruction leaked its upload ticket");
        uploads.drain();
        require(weakSource.expired() &&
                    uploads.query(survivor.ticket).state == UploadState::Completed,
                "preparation destruction damaged other work or leaked in-flight references");
        uploads.releaseTicket(survivor.ticket);
        uploads.releaseTicket(retried.ticket);
    }
}
} // namespace
void runVulkanUploadTests(const Device& device)
{
    runPreparationLifetimeTests(device);
    runMeshPreparationTests(device);
    runNoUploadPreparationTests(device);
    runVersionedPreparationTests(device);
    runImageUpdateTests(device);
    auto bytes = std::make_shared<std::vector<std::byte>>(16, std::byte{0x5a});
    auto a = destination(device);
    auto b = destination(device);
    VulkanUploadService uploads(device, {64, 128, 8});
    auto grouped = request(a, bytes, 16);
    auto secondOperation = request(b, bytes);
    grouped.operations.push_back(std::move(secondOperation.operations.front()));
    auto accepted = uploads.tryEnqueue(grouped);
    require(accepted.accepted() && grouped.operations.empty(), "accepted request was not consumed");
    require(uploads.query(accepted.ticket).state == UploadState::Queued &&
                uploads.stagedBytes() == 0,
            "enqueue performed upload work");
    uploads.tick({16, 1, std::chrono::seconds(1)});
    auto status = uploads.query(accepted.ticket);
    require(status.submittedBytes == 16 && status.completedBytes == 0,
            "partial submission progress incorrect");
    uploads.drain();
    status = uploads.query(accepted.ticket);
    require(status.state == UploadState::Completed && status.completedBytes == 32 &&
                uploads.stagedBytes() == 0,
            "multi-operation request did not complete or staging was retained");
    require(readBack(device, *a, 16, 16) == *bytes && readBack(device, *b, 0, 16) == *bytes,
            "device-local upload data or destination offset incorrect");
    uploads.releaseTicket(accepted.ticket);
    bool staleRejected = false;
    try
    {
        uploads.query(accepted.ticket);
    }
    catch (const std::out_of_range&)
    {
        staleRejected = true;
    }
    require(staleRejected, "released ticket still resolved");

    auto invalid = request(a, bytes, 60);
    require(uploads.tryEnqueue(invalid).code == UploadEnqueueCode::InvalidRequest &&
                invalid.operations.size() == 1,
            "invalid range consumed request");
    auto oversizedBytes = std::make_shared<std::vector<std::byte>>(68);
    auto oversized = request(destination(device, 128), oversizedBytes);
    require(uploads.tryEnqueue(oversized).code == UploadEnqueueCode::InvalidRequest,
            "oversized operation must reject rather than wait forever");

    // Both requests share a submitted batch. Cancelling one must retain its
    // objects until completion and must not cancel the other request.
    auto cancelledBytes = std::make_shared<std::vector<std::byte>>(16, std::byte{0x33});
    auto cancelledTarget = destination(device);
    std::weak_ptr<std::vector<std::byte>> weakSource = cancelledBytes;
    std::weak_ptr<Buffer> weakTarget = cancelledTarget;
    auto cancelled = request(cancelledTarget, cancelledBytes);
    auto survivor = request(destination(device), bytes);
    auto c = uploads.tryEnqueue(cancelled);
    auto d = uploads.tryEnqueue(survivor);
    require(c.accepted() && d.accepted(), "shared batch enqueue failed");
    cancelledBytes.reset();
    cancelledTarget.reset();
    uploads.tick({64, 2, std::chrono::seconds(1)});
    require(uploads.query(c.ticket).submittedBytes == 16 &&
                uploads.query(d.ticket).submittedBytes == 16,
            "requests were not merged into the test batch");
    uploads.cancel(c.ticket);
    uploads.releaseTicket(c.ticket);
    require(!weakSource.expired() && !weakTarget.expired(),
            "cancel freed submitted source or destination");
    uploads.drain();
    require(weakSource.expired() && weakTarget.expired() &&
                uploads.query(d.ticket).state == UploadState::Completed,
            "batch cancellation leaked leases or damaged another request");
    uploads.releaseTicket(d.ticket);

    // Cancel a partially completed request: the not-yet-submitted target is
    // released, completed progress stays stable and no second operation executes.
    auto remainder = destination(device);
    std::weak_ptr<Buffer> weakRemainder = remainder;
    auto partial = request(destination(device), bytes);
    auto last = request(remainder, bytes);
    partial.operations.push_back(std::move(last.operations.front()));
    remainder.reset();
    auto p = uploads.tryEnqueue(partial);
    require(p.accepted(), "partial cancellation enqueue failed");
    uploads.tick({16, 1, std::chrono::seconds(1)});
    device.waitIdle();
    uploads.tick({0, 0, std::chrono::microseconds{0}});
    require(uploads.query(p.ticket).completedBytes == 16,
            "first partial operation did not complete");
    uploads.cancel(p.ticket);
    uploads.drain();
    require(weakRemainder.expired() && uploads.query(p.ticket).submittedBytes == 16 &&
                uploads.query(p.ticket).state == UploadState::Cancelled,
            "partial cancellation submitted or retained the remaining operation");
    uploads.releaseTicket(p.ticket);

    // Queue rejection preserves data; queued cancellation requires no GPU work.
    VulkanUploadService bounded(device, {64, 16, 1});
    auto queuedTarget = destination(device);
    std::weak_ptr<Buffer> weakQueued = queuedTarget;
    auto q = request(queuedTarget, bytes);
    auto qResult = bounded.tryEnqueue(q);
    queuedTarget.reset();
    auto retry = request(destination(device), bytes);
    require(bounded.tryEnqueue(retry).code == UploadEnqueueCode::QueueFull &&
                retry.operations.size() == 1,
            "queue pressure did not preserve request");
    bounded.cancel(qResult.ticket);
    require(weakQueued.expired(), "queued cancellation retained destination");
    bounded.releaseTicket(qResult.ticket);
    auto retried = bounded.tryEnqueue(retry);
    require(retried.accepted(), "queue did not accept retry after release");
    bounded.shutdown();
    require(bounded.query(retried.ticket).state == UploadState::Cancelled &&
                bounded.stagedBytes() == 0,
            "shutdown left queued work alive");
    bounded.releaseTicket(retried.ticket);

    bool threadRejected = false;
    std::thread other(
        [&]
        {
            try
            {
                uploads.tick();
            }
            catch (const std::logic_error&)
            {
                threadRejected = true;
            }
        });
    other.join();
    require(threadRejected, "upload service accepted a foreign thread");

    Image::CreateInfo imageInfo;
    imageInfo.extent = {2, 2, 1};
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    auto image = std::make_shared<Image>(device, imageInfo);
    ImageUpload imageOp;
    imageOp.destination = image;
    imageOp.before = {VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0};
    imageOp.after = {VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT};
    auto shortBytes = std::make_shared<std::vector<std::byte>>(4);
    imageOp.source = {shortBytes, shortBytes->data(), shortBytes->size()};
    imageOp.range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {2, 2, 1};
    imageOp.regions = {region};
    UploadRequest badImage{{imageOp}};
    require(uploads.tryEnqueue(badImage).code == UploadEnqueueCode::InvalidRequest,
            "image upload accepted a truncated payload");

    imageOp.source = {bytes, bytes->data(), bytes->size()};
    auto rejectImage = [&](ImageUpload op, const char* message)
    {
        UploadRequest request{{std::move(op)}};
        require(uploads.tryEnqueue(request).code == UploadEnqueueCode::InvalidRequest &&
                    request.operations.size() == 1,
                message);
    };
    auto invalidMip = imageOp;
    invalidMip.regions[0].imageSubresource.mipLevel = 1;
    rejectImage(invalidMip, "out-of-range mip accepted");
    auto invalidLayer = imageOp;
    invalidLayer.regions[0].imageSubresource.baseArrayLayer = 1;
    rejectImage(invalidLayer, "out-of-range layer accepted");
    auto duplicate = imageOp;
    duplicate.regions.push_back(duplicate.regions.front());
    rejectImage(duplicate, "duplicate image region accepted");
    auto invalidSync = imageOp;
    invalidSync.after.stages = 0;
    rejectImage(invalidSync, "missing image synchronization accepted");
    auto invalidUsage = imageOp;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    invalidUsage.destination = std::make_shared<Image>(device, imageInfo);
    rejectImage(invalidUsage, "shader read layout accepted without sampled/input attachment usage");

    // The same description accepted by validation must reach the GPU successfully.
    UploadRequest validImage{{imageOp}};
    const auto valid = uploads.tryEnqueue(validImage);
    require(valid.accepted(), "valid image upload rejected by unified validation");
    uploads.drain();
    require(uploads.query(valid.ticket).state == UploadState::Completed,
            "valid image upload failed after enqueue");
    uploads.releaseTicket(valid.ticket);
}
} // namespace rubia::test
