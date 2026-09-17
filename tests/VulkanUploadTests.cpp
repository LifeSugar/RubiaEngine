#include "VulkanUploadTests.hpp"
#include "vulkan/Fence.hpp"
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
} // namespace
void runVulkanUploadTests(const Device& device)
{
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
