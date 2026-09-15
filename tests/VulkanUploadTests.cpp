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
} // namespace
void runVulkanUploadTests(const Device& device)
{
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
    invalidSync.finalStages = 0;
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
