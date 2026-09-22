#include "vulkan/Buffer.hpp"
#include "vulkan/CommandPool.hpp"
#include "vulkan/Device.hpp"
#include "vulkan/Fence.hpp"
#include "vulkan/Framebuffer.hpp"
#include "vulkan/GraphicsPipeline.hpp"
#include "vulkan/Image.hpp"
#include "vulkan/ImageView.hpp"
#include "vulkan/RenderPass.hpp"

#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace rubia::test
{
namespace
{
using namespace rhi::vulkan;

void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

std::vector<uint32_t> shader(const char *stage)
{
    std::ifstream file(std::string(RUBIA_PIPELINE_TEST_SHADER_DIR) + "/pipeline_state." + stage +
                           ".spv",
                       std::ios::binary | std::ios::ate);
    require(bool(file), "pipeline test shader missing");
    const auto size = static_cast<size_t>(file.tellg());
    require(size > 0 && size % sizeof(uint32_t) == 0, "invalid pipeline test SPIR-V size");
    std::vector<uint32_t> result(size / sizeof(uint32_t));
    file.seekg(0);
    file.read(reinterpret_cast<char *>(result.data()), static_cast<std::streamsize>(size));
    require(bool(file), "could not read pipeline test shader");
    return result;
}

void rejects(const Device &device, GraphicsPipeline &pipeline,
             const GraphicsPipeline::CreateInfo &info)
{
    const auto previous = pipeline.get();
    try
    {
        pipeline.create(device, info);
    }
    catch (const std::invalid_argument &)
    {
        require(pipeline.get() == previous, "invalid replacement destroyed the existing pipeline");
        return;
    }
    throw std::runtime_error("invalid pipeline state was accepted");
}
} // namespace

void runGraphicsPipelineCacheTests(const rhi::vulkan::Device& device,
    const rhi::vulkan::GraphicsPipeline::CreateInfo& info, const VkRenderPassCreateInfo& passInfo);

void runGraphicsPipelineTests(const rhi::vulkan::Device &device)
{
    using namespace rhi::vulkan;
    // Two color targets in subpass 1: this fails if either the subpass index or
    // attachment count is still hard-coded. Subpass 0 intentionally does no work.
    VkAttachmentDescription color{};
    color.format = VK_FORMAT_R8G8B8A8_UNORM;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    const std::array<VkAttachmentDescription, 2> attachments{color, color};
    const std::array<VkAttachmentReference, 2> references{
        {{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
         {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}}};
    std::array<VkSubpassDescription, 2> subpasses{};
    for (auto &subpass : subpasses)
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpasses[1].colorAttachmentCount = 2;
    subpasses[1].pColorAttachments = references.data();
    VkSubpassDependency dependency{};
    dependency.srcSubpass = 1;
    dependency.dstSubpass = VK_SUBPASS_EXTERNAL;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
    dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependency.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    VkRenderPassCreateInfo passInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    passInfo.attachmentCount = 2;
    passInfo.pAttachments = attachments.data();
    passInfo.subpassCount = 2;
    passInfo.pSubpasses = subpasses.data();
    passInfo.dependencyCount = 1;
    passInfo.pDependencies = &dependency;
    RenderPass pass(device.get(), passInfo);

    GraphicsPipeline::CreateInfo info;
    info.renderPass = pass.reference();
    info.subpass = 1;
    info.vertexShaderSpirv = shader("vert");
    info.fragmentShaderSpirv = shader("frag");
    info.rasterization.cullMode = VK_CULL_MODE_NONE;
    info.depthStencil.depthTestEnable = VK_FALSE;
    info.depthStencil.depthWriteEnable = VK_FALSE;
    info.dynamicStates.clear();
    info.viewport.viewports = {{0, 0, 2, 2, 0, 1}};
    info.viewport.scissors = {{{0, 0}, {1, 2}}};
    auto &blend = info.colorBlend.attachments[0];
    blend.blendEnable = VK_TRUE;
    blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    const auto secondBlend = blend;
    info.colorBlend.attachments.push_back(secondBlend);
    PipelineStageSpecialization specialization;
    specialization.entries = {{0, 0, sizeof(float)}};
    const float intensity = 0.8f;
    specialization.data.resize(sizeof(intensity));
    std::memcpy(specialization.data.data(), &intensity, sizeof(intensity));
    info.specializations.push_back(std::move(specialization));
    // A copied description owns its specialization bytes and all state arrays.
    const auto copied = info;
    runGraphicsPipelineCacheTests(device, copied, passInfo);
    info.specializations.clear();
    GraphicsPipeline pipeline(device, copied);
    auto maskedInfo = copied;
    maskedInfo.multisample.sampleMask = {0};
    GraphicsPipeline maskedPipeline(device, maskedInfo);

    std::array<Image, 2> images;
    std::array<ImageView, 2> views;
    std::vector<VkImageView> viewHandles;
    for (size_t i = 0; i < images.size(); ++i)
    {
        Image::CreateInfo imageInfo;
        imageInfo.extent = {2, 2, 1};
        imageInfo.format = color.format;
        imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        imageInfo.memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        images[i].create(device, imageInfo);
        ImageView::CreateInfo viewInfo;
        viewInfo.image = images[i].get();
        viewInfo.format = color.format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        views[i].create(device.get(), viewInfo);
        viewHandles.push_back(views[i].get());
    }
    Framebuffer framebuffer(device.get(), pass.get(), viewHandles, {2, 2});
    Buffer readback(device, 32, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    CommandPool pool(device, device.graphicsQueueFamily());
    const auto cb = pool.allocatePrimary();
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    require(vkBeginCommandBuffer(cb, &begin) == VK_SUCCESS, "pipeline test begin failed");
    std::array<VkClearValue, 2> clear{};
    for (auto &value : clear)
        value.color.float32[1] = 0.2f;
    VkRenderPassBeginInfo render{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    render.renderPass = pass.get();
    render.framebuffer = framebuffer.get();
    render.renderArea.extent = {2, 2};
    render.clearValueCount = 2;
    render.pClearValues = clear.data();
    vkCmdBeginRenderPass(cb, &render, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdNextSubpass(cb, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.get());
    vkCmdDraw(cb, 3, 1, 0, 0);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, maskedPipeline.get());
    vkCmdDraw(cb, 3, 1, 0, 0); // Masked out: must not blend a second time.
    vkCmdEndRenderPass(cb);
    for (size_t i = 0; i < images.size(); ++i)
    {
        VkBufferImageCopy region{};
        region.bufferOffset = i * 16;
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {2, 2, 1};
        vkCmdCopyImageToBuffer(cb, images[i].get(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               readback.get(), 1, &region);
    }
    VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = readback.get();
    barrier.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0,
                         nullptr, 1, &barrier, 0, nullptr);
    require(vkEndCommandBuffer(cb) == VK_SUCCESS, "pipeline test end failed");
    Fence fence(device.get());
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cb;
    require(vkQueueSubmit(device.graphicsQueue(), 1, &submit, fence.get()) == VK_SUCCESS,
            "pipeline test submit failed");
    fence.wait();
    const auto *bytes = static_cast<const uint8_t *>(readback.map());
    for (size_t target = 0; target < 2; ++target)
    {
        for (size_t y = 0; y < 2; ++y)
        {
            const auto *pixel = bytes + target * 16 + y * 8;
            require(std::abs(int(pixel[target == 0 ? 0 : 2]) - 102) <= 1 &&
                        std::abs(int(pixel[1]) - 26) <= 1 && std::abs(int(pixel[3]) - 128) <= 1,
                    "MRT blend/specialization/sample-mask output mismatch");
            require(pixel[4] == 0 && pixel[5] == 51 && pixel[6] == 0 && pixel[7] == 0,
                    "static scissor did not preserve the other column");
        }
    }
    readback.unmap();

    // Shape errors and unavailable features must fail before calling Vulkan.
    auto bad = copied;
    bad.multisample.samples = static_cast<VkSampleCountFlagBits>(3);
    rejects(device, pipeline, bad);
    bad = copied;
    bad.multisample.sampleMask = {~0u, ~0u};
    rejects(device, pipeline, bad);
    bad = copied;
    bad.dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_VIEWPORT};
    rejects(device, pipeline, bad);
    bad = copied;
    bad.viewport.scissors.clear();
    rejects(device, pipeline, bad);
    bad = copied;
    bad.specializations[0].entries[0].offset = 4;
    rejects(device, pipeline, bad);
    bad = copied;
    bad.specializations[0].entries.push_back(bad.specializations[0].entries[0]);
    rejects(device, pipeline, bad);
    bad = copied;
    bad.inputAssembly.primitiveRestartEnable = VK_TRUE;
    rejects(device, pipeline, bad);
    if (!device.enabledFeatures().fillModeNonSolid)
    {
        bad = copied;
        bad.rasterization.polygonMode = VK_POLYGON_MODE_LINE;
        rejects(device, pipeline, bad);
    }
    if (!device.enabledFeatures().independentBlend)
    {
        bad = copied;
        bad.colorBlend.attachments[1].colorWriteMask = VK_COLOR_COMPONENT_R_BIT;
        rejects(device, pipeline, bad);
    }

    // A depth/stencil-only pipeline needs neither an FS nor color blend slots.
    auto depthAttachment = color;
    depthAttachment.format = device.findDepthStencilFormat();
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkAttachmentReference depthReference{0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription depthSubpass{};
    depthSubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    depthSubpass.pDepthStencilAttachment = &depthReference;
    passInfo.attachmentCount = 1;
    passInfo.pAttachments = &depthAttachment;
    passInfo.subpassCount = 1;
    passInfo.pSubpasses = &depthSubpass;
    passInfo.dependencyCount = 0;
    passInfo.pDependencies = nullptr;
    RenderPass depthPass(device.get(), passInfo);
    auto depthInfo = copied;
    depthInfo.renderPass = depthPass.reference();
    depthInfo.subpass = 0;
    depthInfo.fragmentShaderSpirv.clear();
    depthInfo.specializations.clear();
    depthInfo.colorBlend.attachments.clear();
    depthInfo.depthStencil.depthTestEnable = VK_TRUE;
    depthInfo.depthStencil.depthWriteEnable = VK_TRUE;
    depthInfo.depthStencil.stencilTestEnable = VK_TRUE;
    depthInfo.depthStencil.front.passOp = VK_STENCIL_OP_REPLACE;
    depthInfo.depthStencil.front.reference = 7;
    depthInfo.rasterization.depthBiasEnable = VK_TRUE;
    depthInfo.rasterization.depthBiasConstantFactor = 1.0f;
    depthInfo.rasterization.depthBiasSlopeFactor = 1.0f;
    GraphicsPipeline depthPipeline(device, depthInfo);
    require(bool(depthPipeline), "depth-only pipeline creation failed");
    std::cout << "Graphics pipeline state tests passed (GPU readback + rejection cases).\n";
}
} // namespace rubia::test
