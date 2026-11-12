#include "vulkan/Device.hpp"
#include "vulkan/GraphicsPipelineCache.hpp"
#include <algorithm>
#include <cstring>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace rubia::test
{
namespace
{
using namespace rhi::vulkan;
using Info = GraphicsPipeline::CreateInfo;
void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}
template <class Action> void rejects(Action action)
{
    try
    {
        action();
    }
    catch (const std::invalid_argument &)
    {
        return;
    }
    throw std::runtime_error("pipeline cache accepted an invalid request");
}
} // namespace

void runGraphicsPipelineCacheTests(const rhi::vulkan::Device &device, const Info &base,
                                   const VkRenderPassCreateInfo &passInfo)
{
    GraphicsPipelineCache cache(device);
    require(!cache.find(base) && cache.size() == 0, "find unexpectedly created a pipeline");
    const auto first = cache.getOrCreate(base);
    require(cache.getOrCreate(base) == first && cache.find(base) == first &&
                cache.statistics().creations == 1 && cache.statistics().hits == 1,
            "identical pipeline requests did not reuse the object");

    // Core render-pass compatibility must use values, not a VkRenderPass address.
    auto attachments = std::vector<VkAttachmentDescription>(
        passInfo.pAttachments, passInfo.pAttachments + passInfo.attachmentCount);
    auto compatibleInfo = passInfo;
    for (auto &a : attachments)
    {
        a.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        a.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a.finalLayout = VK_IMAGE_LAYOUT_GENERAL;
    }
    compatibleInfo.pAttachments = attachments.data();
    RenderPass compatible(device.get(), compatibleInfo);
    auto request = base;
    request.renderPass = compatible.reference();
    require(cache.getOrCreate(request) == first,
            "load/store/layout-only pass change missed the cache");
    const auto oldPass = request.renderPass;
    compatible.reset();
    require(oldPass->get() != VK_NULL_HANDLE && cache.getOrCreate(request) == first,
            "render pass snapshot did not survive owner reset");

    // Attachment formats, dependencies and subpass selection remain significant.
    const auto key = GraphicsPipelineKey::from(base);
    attachments[0].format = VK_FORMAT_R16G16B16A16_SFLOAT;
    RenderPass changedPass(device.get(), compatibleInfo);
    request.renderPass = changedPass.reference();
    require(GraphicsPipelineKey::from(request) != key, "attachment format missing from key");
    attachments[0].format = passInfo.pAttachments[0].format;
    auto dependency = passInfo.pDependencies[0];
    dependency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    compatibleInfo.pDependencies = &dependency;
    RenderPass changedDependency(device.get(), compatibleInfo);
    request.renderPass = changedDependency.reference();
    require(GraphicsPipelineKey::from(request) != key, "render pass dependency missing from key");

    // Check independent categories that used to be absent from PSO selection.
    const std::vector<std::function<void(Info &)>> changes = {
        [](auto &i) { i.subpass = 0; },
        [](auto &i) { i.vertexShaderSpirv[2] ^= 1; }, // SPIR-V generator word, still valid SPIR-V.
        [](auto &i) { i.vertexEntryPoint = "anotherEntry"; },
        [](auto &i) { i.vertexBindings = {{0, 16, VK_VERTEX_INPUT_RATE_VERTEX}}; },
        [](auto &i) { i.inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP; },
        [](auto &i) { i.rasterization.cullMode = VK_CULL_MODE_BACK_BIT; },
        [](auto &i) { i.depthStencil.depthTestEnable = VK_TRUE; },
        [](auto &i) { i.depthStencil.front.passOp = VK_STENCIL_OP_REPLACE; },
        [](auto &i) { i.multisample.samples = VK_SAMPLE_COUNT_4_BIT; },
        [](auto &i) { i.multisample.sampleMask = {0}; },
        [](auto &i) { i.colorBlend.attachments[0].blendEnable = VK_FALSE; },
        [](auto &i) { i.colorBlend.constants[0] = 0.5f; },
        [](auto &i) { i.viewport.viewports[0].width = 16; },
        [](auto &i) { i.viewport.scissors[0].extent.width = 16; },
        [](auto &i) { i.pushConstantRanges = {{VK_SHADER_STAGE_VERTEX_BIT, 0, 4}}; },
        [](auto &i) { i.specializations[0].data[0] ^= std::byte{1}; }};
    for (const auto &change : changes)
    {
        request = base;
        change(request);
        require(GraphicsPipelineKey::from(request) != key,
                "pipeline-affecting state missing from key");
    }
    request = base;
    request.vertexShaderSpirv[2] ^= 1;
    require(cache.getOrCreate(request) != first && cache.statistics().creations == 2,
            "changed shader contents reused an old pipeline");

    // Dynamic values and irrelevant host layout must not create new PSOs.
    request = base;
    request.dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT,
                             VK_DYNAMIC_STATE_SCISSOR,
                             VK_DYNAMIC_STATE_BLEND_CONSTANTS,
                             VK_DYNAMIC_STATE_DEPTH_BIAS,
                             VK_DYNAMIC_STATE_LINE_WIDTH,
                             VK_DYNAMIC_STATE_DEPTH_BOUNDS,
                             VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
                             VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
                             VK_DYNAMIC_STATE_STENCIL_REFERENCE};
    const auto dynamicKey = GraphicsPipelineKey::from(request);
    std::reverse(request.dynamicStates.begin(), request.dynamicStates.end());
    request.viewport.viewports[0].width = 999;
    request.viewport.scissors[0].extent.height = 999;
    request.colorBlend.constants = {1, 2, 3, 4};
    request.rasterization.depthBiasSlopeFactor = 10;
    request.rasterization.lineWidth = 2;
    request.depthStencil.maxDepthBounds = 0.5f;
    request.depthStencil.front.compareMask = request.depthStencil.back.writeMask = 17;
    request.depthStencil.front.reference = request.depthStencil.back.reference = 19;
    require(GraphicsPipelineKey::from(request) == dynamicKey,
            "dynamic values leaked into pipeline key");
    request = base;
    request.multisample.sampleMask = {~0u};
    request.specializations[0].data.insert(request.specializations[0].data.begin(), 8,
                                           std::byte{0});
    request.specializations[0].entries[0].offset = 8;
    require(cache.getOrCreate(request) == first,
            "equivalent masks/specialization packing did not reuse pipeline");

    // Distinct descriptor handles with identical definitions must share a PSO.
    std::vector<VkDescriptorSetLayoutBinding> bindings = {
        {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
        {3, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
    DescriptorSetLayout layoutA(device.get(), bindings);
    std::reverse(bindings.begin(), bindings.end());
    DescriptorSetLayout layoutB(device.get(), bindings);
    request = base;
    request.descriptorSetLayouts = {layoutA.reference()};
    auto layoutPipeline = cache.getOrCreate(request);
    request.descriptorSetLayouts = {layoutB.reference()};
    require(cache.getOrCreate(request) == layoutPipeline,
            "layout handle/order leaked into pipeline key");
    bindings[0].descriptorCount = 2;
    DescriptorSetLayout changedLayout(device.get(), bindings);
    auto changed = request;
    changed.descriptorSetLayouts = {changedLayout.reference()};
    require(GraphicsPipelineKey::from(changed) != GraphicsPipelineKey::from(request),
            "descriptor layout content missing from key");
    layoutB.reset();
    require(cache.getOrCreate(request) == layoutPipeline,
            "descriptor snapshot did not survive owner reset");

    // Even a deliberate hash collision still compares full state.
    struct Collision
    {
        size_t operator()(const GraphicsPipelineKey &) const
        {
            return 0;
        }
    };
    std::unordered_map<GraphicsPipelineKey, int, Collision> collisions;
    collisions.emplace(key, 1);
    collisions.emplace(GraphicsPipelineKey::from(changed), 2);
    require(collisions.size() == 2, "pipeline key equality used only a hash");

    const auto beforeFailure = cache.statistics().creations;
    const auto count = cache.size();
    request = base;
    request.subpass = 99;
    rejects([&] { cache.getOrCreate(request); });
    request = base;
    request.dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_VIEWPORT};
    rejects([&] { cache.getOrCreate(request); });
    request = base;
    request.specializations[0].entries.push_back(request.specializations[0].entries[0]);
    rejects([&] { cache.getOrCreate(request); });
    require(cache.size() == count && cache.statistics().creations == beforeFailure,
            "invalid request polluted pipeline cache");

    bool wrongThreadRejected = false;
    std::thread wrongThread([&] {
        try
        {
            cache.getOrCreate(base);
        }
        catch (const std::logic_error &)
        {
            wrongThreadRejected = true;
        }
    });
    wrongThread.join();
    require(wrongThreadRejected, "pipeline cache accepted another thread");

    GraphicsPipelineCache temporary(device);
    auto held = temporary.getOrCreate(base);
    std::weak_ptr<const GraphicsPipeline> weak = held;
    temporary.clear();
    require(!weak.expired() && bool(*held), "cache clear destroyed a retained pipeline");
    held.reset();
    require(weak.expired(), "pipeline leaked after cache and consumers released ownership");
    std::cout << "Graphics pipeline cache tests passed (keys, reuse, lifetime, rejection).\n";
}
} // namespace rubia::test
