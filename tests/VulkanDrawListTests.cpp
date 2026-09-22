#include "asset/AssetManager.hpp"
#include "gltf/GLBMaterialImporter.hpp"
#include "render/SceneResourcePreparation.hpp"
#include "shader/SpirvShaderImporter.hpp"
#include "vulkan/Buffer.hpp"
#include "vulkan/CommandPool.hpp"
#include "vulkan/Device.hpp"
#include "vulkan/Fence.hpp"
#include "vulkan/FrameDataResources.hpp"
#include "vulkan/Framebuffer.hpp"
#include "vulkan/GraphicsPipelineCache.hpp"
#include "vulkan/Image.hpp"
#include "vulkan/ImageView.hpp"
#include "vulkan/RenderAssetCache.hpp"
#include "vulkan/RetiredResources.hpp"
#include "vulkan/VulkanDrawListCompiler.hpp"
#include "vulkan/VulkanDrawListRecorder.hpp"
#include "vulkan/VulkanResourcePreparation.hpp"
#include "vulkan/VulkanUploadService.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>

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

template <class F> void rejects(F &&action)
{
    try
    {
        action();
    }
    catch (const std::invalid_argument &)
    {
        return;
    }
    throw std::runtime_error("invalid draw compilation was accepted");
}
} // namespace

void runPipelinePreparationTests(const rhi::vulkan::Device&, const rhi::vulkan::GraphicsPipeline::CreateInfo&);

void runVulkanDrawListTests(const rhi::vulkan::Device &device)
{
    using namespace rhi::vulkan;
    asset::AssetManager assets;
    auto shader = [&](const char *file, asset::ShaderStage stage) {
        importer::shader::SpirvShaderImporter::CreateInfo info;
        info.assets = &assets;
        info.path = std::string(RUBIA_PIPELINE_TEST_SHADER_DIR) + "/" + file + ".spv";
        info.name = file;
        info.stage = stage;
        return importer::shader::SpirvShaderImporter{}.import(info);
    };
    const auto vs = shader("draw_list.vert", asset::ShaderStage::Vertex);
    const auto fsA = shader("draw_list.frag", asset::ShaderStage::Fragment);
    const auto fsB = shader("draw_list_alternate.frag", asset::ShaderStage::Fragment);
    const auto programA = assets.createShaderProgram({"Draw A", {vs, fsA}});
    const auto programB = assets.createShaderProgram({"Draw B", {vs, fsB}});
    const auto templateA = assets.createMaterialTemplate({"Template A", programA});
    const auto templateB = assets.createMaterialTemplate({"Template B", programB});
    auto opaqueState = asset::makeOpaqueMaterialState();
    opaqueState.doubleSided = true;
    opaqueState.depth.testEnabled = false;
    opaqueState.depth.writeEnabled = false;
    auto transparentState = asset::makeTransparentMaterialState();
    transparentState.doubleSided = true;
    transparentState.depth.testEnabled = false;
    auto material = [&](asset::MaterialTemplateAssetHandle handle, glm::vec4 color,
                        asset::MaterialRenderState state) {
        asset::MaterialAsset::CreateInfo info;
        info.name = "Draw material";
        info.materialTemplate = handle;
        info.renderState = state;
        info.parameters = {{"tint", color}};
        return assets.createMaterial(std::move(info));
    };
    const auto red = material(templateA, {1, 0, 0, 1}, opaqueState);
    const auto green = material(templateB, {0, 1, 0, 1}, opaqueState);
    const auto blue = material(templateA, {0, 0, 1, 0.5f}, transparentState);
    const auto overlay = material(templateB, {1, 0, 0, 0.5f}, transparentState);
    const auto otherRed = material(templateA, {0.8f, 0, 0, 1}, opaqueState);

    asset::MeshAsset::CreateInfo meshInfo;
    meshInfo.name = "Draw quad";
    meshInfo.vertices.resize(4);
    meshInfo.vertices[0].position = {-1, -1, 0.5f};
    meshInfo.vertices[1].position = {1, -1, 0.5f};
    meshInfo.vertices[2].position = {1, 1, 0.5f};
    meshInfo.vertices[3].position = {-1, 1, 0.5f};
    meshInfo.indices = {0, 1, 2, 0, 2, 3};
    meshInfo.submeshes = {{0, 4, 0, 6, red}, {0, 4, 0, 6, green}};
    const auto mesh = assets.createMesh(meshInfo);
    // Also exercise the non-indexed recording branch.
    const auto quadVertices = meshInfo.vertices;
    meshInfo.vertices.clear();
    for (auto index : meshInfo.indices)
        meshInfo.vertices.push_back(quadVertices[index]);
    meshInfo.indices.clear();
    meshInfo.submeshes = {{0, 6, 0, 0, blue}};
    const auto nonIndexed = assets.createMesh(meshInfo);
    asset::ModelAsset::CreateInfo modelInfo;
    modelInfo.name = "Multiple templates";
    modelInfo.nodes.resize(1);
    modelInfo.nodes[0].meshes = {mesh, nonIndexed};
    const auto model = assets.createModel(std::move(modelInfo));
    const auto sceneRequest =
        render::makeSceneResourceRequest(assets, {model}, templateA, programA, 4);
    require(sceneRequest.meshes.size() == 2, "scene request rejected multiple material templates");

    VulkanUploadService uploads(device);
    RenderAssetCache resources;
    RetiredResources retired;
    VulkanResourcePreparation preparations(device, uploads, retired);
    auto finish = [&](ResourcePreparationResult result) {
        require(result.accepted(), "draw fixture preparation rejected");
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        for (;;)
        {
            uploads.drain();
            preparations.advance();
            const auto status = preparations.status(result.ticket);
            if (status.state == ResourcePreparationState::Ready)
                break;
            if (resourcePreparationFinished(status.state))
                throw std::runtime_error("draw fixture preparation failed: " + status.error);
            require(std::chrono::steady_clock::now() < deadline,
                    "draw fixture preparation timed out");
        }
        preparations.release(result.ticket);
    };
    finish(preparations.prepareMesh(resources, assets.snapshot(mesh)));
    finish(preparations.prepareMesh(resources, assets.snapshot(nonIndexed)));
    finish(preparations.prepareMaterial(resources, assets.snapshot(overlay)));
    finish(preparations.prepareMaterial(resources, assets.snapshot(otherRed)));

    VkAttachmentDescription attachment{};
    attachment.format = VK_FORMAT_R8G8B8A8_UNORM;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    VkAttachmentReference reference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &reference;
    VkSubpassDependency dependency{};
    dependency.srcSubpass = 0;
    dependency.dstSubpass = VK_SUBPASS_EXTERNAL;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
    dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependency.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    VkRenderPassCreateInfo passInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    passInfo.attachmentCount = 1;
    passInfo.pAttachments = &attachment;
    passInfo.subpassCount = 1;
    passInfo.pSubpasses = &subpass;
    passInfo.dependencyCount = 1;
    passInfo.pDependencies = &dependency;
    RenderPass pass(device.get(), passInfo);
    FrameDataResources frames(device, 1, 4);
    GraphicsPipelineCache pipelines(device);
    GraphicsPipeline::CreateInfo passPipeline;
    passPipeline.renderPass = pass.reference();
    passPipeline.descriptorSetLayouts = {frames.descriptorSetLayoutReference()};
    render::RenderList list;
    list.objectData.resize(4);
    list.objectData[0].world[0][0] = list.objectData[1].world[0][0] = 0.5f;
    list.objectData[0].world[3][0] = -0.5f;
    list.objectData[1].world[3][0] = 0.5f;
    auto item = [&](asset::MaterialAssetHandle handle, uint32_t objectIndex) {
        render::RenderItem result;
        result.mesh = mesh;
        result.material = handle;
        result.materialKey = render::makeMaterialKey(handle);
        const auto &source = assets.material(handle);
        result.pipelineKey =
            render::makePipelineVariantKey(source.materialTemplate(), source.renderState());
        result.queue = render::renderQueueFor(source.renderState());
        result.objectIndex = objectIndex;
        return result;
    };
    list.opaque = {item(red, 0), item(green, 1)};
    list.opaque[1].submeshIndex = 1;
    list.transparent = {item(blue, 2), item(overlay, 3)};
    list.transparent[0].mesh = nonIndexed;
    auto compile = [&](const render::RenderList &input) {
        return VulkanDrawListCompiler{}.compile(input, resources, passPipeline, pipelines);
    };
    runPipelinePreparationTests(device, VulkanDrawListCompiler::describeMaterialPipeline(
        *resources.materialTemplateForMaterial(red), list.opaque[0].pipelineKey, passPipeline));
    auto compiled = compile(list);
    require(pipelines.statistics().creations == 4 && compiled.size() == 4,
            "draw variants did not resolve four distinct pipelines");
    const auto warm = compile(list);
    require(pipelines.statistics().creations == 4 && pipelines.statistics().hits == 4 &&
                warm.opaque[0].pipeline == compiled.opaque[0].pipeline,
            "prewarm and draw compilation did not share pipeline cache entries");
    auto repeated = list;
    repeated.opaque.push_back(item(otherRed, 0));
    const auto requests = pipelines.statistics().requests;
    const auto reused = compile(repeated);
    require(reused.opaque[0].pipeline == reused.opaque.back().pipeline &&
                pipelines.statistics().requests == requests + 4,
            "material parameter changes rebuilt a PSO or repeated draws rebuilt its full key");

    auto clippedState = opaqueState;
    clippedState.alphaClipEnabled = true;
    const auto clipped = material(templateB, {1, 1, 1, 1}, clippedState);
    finish(preparations.prepareMaterial(resources, assets.snapshot(clipped)));
    auto invalid = list;
    invalid.opaque[0] = item(clipped, 0);
    rejects([&] { compile(invalid); });
    invalid = list;
    invalid.opaque[0].pipelineKey.depthWriteEnabled = true;
    rejects([&] { compile(invalid); });
    invalid = list;
    ++invalid.opaque[0].material.generation;
    invalid.opaque[0].materialKey = render::makeMaterialKey(invalid.opaque[0].material);
    rejects([&] { compile(invalid); });
    const auto originalPass = passPipeline;
    passPipeline.dynamicStates.push_back(VK_DYNAMIC_STATE_LINE_WIDTH);
    rejects([&] { compile(list); });
    passPipeline = originalPass;
    DescriptorSetLayout emptyFrame(device.get(), {});
    passPipeline.descriptorSetLayouts[0] = emptyFrame.reference();
    rejects([&] { compile(list); });
    passPipeline = originalPass;

    // Cull/depth semantic changes must create variants even with the same program/layout.
    auto changedState = opaqueState;
    changedState.doubleSided = false;
    const auto culled = material(templateA, {1, 0, 0, 1}, changedState);
    finish(preparations.prepareMaterial(resources, assets.snapshot(culled)));
    auto variantList = list;
    variantList.opaque[0] = item(culled, 0);
    require(compile(variantList).opaque[0].pipeline != compiled.opaque[0].pipeline,
            "cull state did not select a pipeline variant");
    changedState = opaqueState;
    changedState.depth.testEnabled = true;
    changedState.depth.writeEnabled = true;
    changedState.depth.compare = asset::DepthCompare::Greater;
    const auto depth = material(templateA, {1, 0, 0, 1}, changedState);
    finish(preparations.prepareMaterial(resources, assets.snapshot(depth)));
    variantList.opaque[0] = item(depth, 0);
    require(compile(variantList).opaque[0].pipeline != compiled.opaque[0].pipeline,
            "depth state did not select a pipeline variant");

    // Different thresholds remain material data and share the same enabled PSO.
    const std::array<float, 8> alphaValues{0.25f, 0.25f, 0.25f, 0.25f, 0, 1, 0.999f, 0.25f};
    const std::array<float, 8> thresholds{0.5f, 0.2f, 0.5f, 0.25f, 0, 1, 1, 1};
    std::vector<asset::MaterialAssetHandle> clipMaterials;
    for (size_t i = 0; i < alphaValues.size(); ++i)
    {
        auto state = asset::makeOpaqueMaterialState();
        state.doubleSided = true;
        state.alphaClipEnabled = i != 0 && i != 7;
        state.alphaClipThreshold = thresholds[i];
        const auto handle = material(templateA, {1, 0, 0, alphaValues[i]}, state);
        finish(preparations.prepareMaterial(resources, assets.snapshot(handle)));
        clipMaterials.push_back(handle);
    }
    auto backgroundState = asset::makeOpaqueMaterialState();
    backgroundState.doubleSided = true;
    const auto background = material(templateB, {0, 1, 0, 1}, backgroundState);
    finish(preparations.prepareMaterial(resources, assets.snapshot(background)));

    importer::gltf::GLBMaterial maskSource;
    maskSource.alphaMode = "MASK";
    maskSource.alphaCutoff = 0.3f;
    maskSource.doubleSided = true;
    importer::gltf::GLBMaterialImporter::CreateInfo importInfo;
    importInfo.assets = &assets;
    importInfo.mapping.materialTemplate = templateA;
    importInfo.mapping.baseColorParameter = "tint";
    const auto imported = importer::gltf::GLBMaterialImporter{}.import({maskSource}, importInfo);
    const auto &maskState = assets.material(imported.front()).renderState();
    require(maskState.alphaClipEnabled && maskState.alphaClipThreshold == 0.3f &&
                maskState.doubleSided && maskState.depth.writeEnabled && maskState.opaque(),
            "glTF MASK/alphaCutoff did not reach material state");

    // Simulate a new template version being published before its material version.
    // The resident material must continue using its original program/layout snapshot.
    const auto oldTemplate = resources.materialTemplateForMaterial(red);
    auto target = preparationTarget(resources, assets.snapshot(templateB));
    target.asset = templateA;
    target.revision = assets.contentRevision(templateA) + 1;
    resources.acceptPreparation(target);
    resources.publishPreparedMaterialTemplate(retired, target,
                                              resources.materialTemplate(templateB));
    require(resources.materialTemplate(templateA) != oldTemplate &&
                resources.materialTemplateForMaterial(red) == oldTemplate &&
                compile(list).opaque[0].pipeline == compiled.opaque[0].pipeline,
            "draw compilation mixed resident material descriptors with a newer template");

    // Preparation can grow residency arrays; resolve borrowed pointers after publication.
    compiled = compile(list);
    frames.setCameraData(render::CameraGpuData{});
    frames.setObjectData(list.objectData.data(), 4);
    frames.sync(0);
    Image::CreateInfo imageInfo;
    imageInfo.extent = {2, 2, 1};
    imageInfo.format = attachment.format;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    imageInfo.memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    Image image(device, imageInfo);
    ImageView::CreateInfo viewInfo;
    viewInfo.image = image.get();
    viewInfo.format = attachment.format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ImageView view(device.get(), viewInfo);
    Framebuffer framebuffer(device.get(), pass.get(), {view.get()}, {2, 2});
    Buffer readback(device, 16, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    CommandPool pool(device, device.graphicsQueueFamily());
    const auto cb = pool.allocatePrimary();
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    require(vkBeginCommandBuffer(cb, &begin) == VK_SUCCESS, "draw test begin failed");
    VkClearValue clear{};
    VkRenderPassBeginInfo render{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    render.renderPass = pass.get();
    render.framebuffer = framebuffer.get();
    render.renderArea.extent = {2, 2};
    render.clearValueCount = 1;
    render.pClearValues = &clear;
    vkCmdBeginRenderPass(cb, &render, VK_SUBPASS_CONTENTS_INLINE);
    // Compiled pipeline owners survive eviction of the application cache.
    pipelines.clear();
    recordVulkanDrawList(cb, frames.descriptorSet(0), {2, 2}, compiled);
    vkCmdEndRenderPass(cb);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {2, 2, 1};
    vkCmdCopyImageToBuffer(cb, image.get(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.get(), 1,
                           &copy);
    VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = readback.get();
    barrier.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0,
                         nullptr, 1, &barrier, 0, nullptr);
    require(vkEndCommandBuffer(cb) == VK_SUCCESS, "draw test end failed");
    Fence fence(device.get());
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cb;
    require(vkQueueSubmit(device.graphicsQueue(), 1, &submit, fence.get()) == VK_SUCCESS,
            "draw test submit failed");
    fence.wait();
    const auto *bytes = static_cast<const uint8_t *>(readback.map());
    for (size_t y = 0; y < 2; ++y)
        for (size_t x = 0; x < 2; ++x)
        {
            const auto *pixel = bytes + (y * 2 + x) * 4;
            const std::array<int, 4> expected =
                x == 0 ? std::array<int, 4>{191, 0, 64, 255} : std::array<int, 4>{128, 64, 64, 255};
            for (size_t c = 0; c < 4; ++c)
                require(std::abs(int(pixel[c]) - expected[c]) <= 1,
                        "draw switching/readback mismatch: program, layout, object index or "
                        "transparency order");
        }
    readback.unmap();
    // Eight columns: disabled, below/above/equal threshold, 0 and 1 boundaries.
    // Draw a farther green background last: discarded fragments must not write depth.
    auto depthAttachment = attachment;
    depthAttachment.format = device.findDepthStencilFormat();
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    const std::array<VkAttachmentDescription, 2> clipAttachments{attachment, depthAttachment};
    VkAttachmentReference depthReference{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    subpass.pDepthStencilAttachment = &depthReference;
    passInfo.attachmentCount = 2;
    passInfo.pAttachments = clipAttachments.data();
    RenderPass clipPass(device.get(), passInfo);
    FrameDataResources clipFrames(device, 1, 9);
    render::RenderList clipList;
    clipList.objectData.resize(9);
    for (uint32_t i = 0; i < 8; ++i)
    {
        clipList.opaque.push_back(item(clipMaterials[i], i));
        auto &world = clipList.objectData[i].world;
        world[0][0] = 1.0f / 8.0f;
        world[3][0] = -1.0f + (2.0f * i + 1.0f) / 8.0f;
        world[3][2] = -0.25f;
    }
    clipList.opaque.push_back(item(background, 8));
    clipList.objectData[8].world[3][2] = 0.25f;
    auto clipPipeline = originalPass;
    clipPipeline.renderPass = clipPass.reference();
    clipPipeline.descriptorSetLayouts = {clipFrames.descriptorSetLayoutReference()};
    const auto clipDraws =
        VulkanDrawListCompiler{}.compile(clipList, resources, clipPipeline, pipelines);
    for (size_t i = 2; i < 7; ++i)
        require(clipDraws.opaque[i].pipeline == clipDraws.opaque[1].pipeline,
                "AlphaClip threshold changed the Pipeline key");
    require(clipDraws.opaque[0].pipeline == clipDraws.opaque[7].pipeline &&
                clipDraws.opaque[0].pipeline != clipDraws.opaque[1].pipeline,
            "AlphaClip on/off failed to select different specialization variants");
    clipFrames.setCameraData(render::CameraGpuData{});
    clipFrames.setObjectData(clipList.objectData.data(), 9);
    clipFrames.sync(0);
    imageInfo.extent = {8, 1, 1};
    Image clipImage(device, imageInfo);
    viewInfo.image = clipImage.get();
    ImageView clipView(device.get(), viewInfo);
    auto depthImageInfo = imageInfo;
    depthImageInfo.format = depthAttachment.format;
    depthImageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    Image depthImage(device, depthImageInfo);
    auto depthViewInfo = viewInfo;
    depthViewInfo.image = depthImage.get();
    depthViewInfo.format = depthAttachment.format;
    depthViewInfo.subresourceRange.aspectMask =
        VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    ImageView depthView(device.get(), depthViewInfo);
    Framebuffer clipFramebuffer(device.get(), clipPass.get(), {clipView.get(), depthView.get()},
                                {8, 1});
    Buffer clipReadback(device, 32, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    pool.resetCommands();
    require(vkBeginCommandBuffer(cb, &begin) == VK_SUCCESS, "AlphaClip test begin failed");
    std::array<VkClearValue, 2> clipClear{};
    clipClear[1].depthStencil = {1.0f, 0};
    render.renderPass = clipPass.get();
    render.framebuffer = clipFramebuffer.get();
    render.renderArea.extent = {8, 1};
    render.clearValueCount = 2;
    render.pClearValues = clipClear.data();
    vkCmdBeginRenderPass(cb, &render, VK_SUBPASS_CONTENTS_INLINE);
    recordVulkanDrawList(cb, clipFrames.descriptorSet(0), {8, 1}, clipDraws);
    vkCmdEndRenderPass(cb);
    copy.imageExtent = {8, 1, 1};
    vkCmdCopyImageToBuffer(cb, clipImage.get(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           clipReadback.get(), 1, &copy);
    barrier.buffer = clipReadback.get();
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0,
                         nullptr, 1, &barrier, 0, nullptr);
    require(vkEndCommandBuffer(cb) == VK_SUCCESS, "AlphaClip test end failed");
    fence.resetSignal();
    require(vkQueueSubmit(device.graphicsQueue(), 1, &submit, fence.get()) == VK_SUCCESS,
            "AlphaClip test submit failed");
    fence.wait();
    const auto *clipBytes = static_cast<const uint8_t *>(clipReadback.map());
    for (size_t i = 0; i < 8; ++i)
    {
        const bool discarded = i == 2 || i == 6;
        const auto *pixel = clipBytes + i * 4;
        require(pixel[0] == (discarded ? 0 : 255) && pixel[1] == (discarded ? 255 : 0) &&
                    pixel[2] == 0,
                "AlphaClip color/depth mismatch: discard, threshold, specialization or depth-hole "
                "failure");
        const int expectedAlpha = discarded ? 255 : int(std::round(alphaValues[i] * 255));
        require(std::abs(int(pixel[3]) - expectedAlpha) <= 1,
                "AlphaClip survivors were blended or used the wrong alpha");
    }
    clipReadback.unmap();
    std::clog << "[Vulkan] AlphaClip tests passed (threshold boundaries, PSO reuse, depth holes)\n";
    std::clog << "[Vulkan] Draw-list pipeline selection and switching tests passed\n";
}
} // namespace rubia::test
