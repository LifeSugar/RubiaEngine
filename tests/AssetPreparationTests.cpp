#include "VulkanUploadTests.hpp"
#include "vulkan/DefaultPipelineFactory.hpp"
#include "vulkan/Device.hpp"
#include "vulkan/RenderAssetCache.hpp"
#include "vulkan/RenderPass.hpp"
#include "vulkan/VulkanResourcePreparation.hpp"
#include "vulkan/VulkanUploadService.hpp"
#include <array>
#include <chrono>
#include <functional>
#include <stdexcept>
#include <type_traits>

namespace rubia::test
{
using namespace rhi::vulkan;
namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}
ResourcePreparationResult prepare(VulkanResourcePreparation& preparations, RenderAssetCache& cache,
                                  asset::AnyAssetSnapshot source)
{
    return std::visit(
        [&](auto value)
        {
            using T = decltype(value);
            if constexpr (std::is_same_v<T, asset::AssetSnapshot<asset::TextureAsset>>)
            {
                return preparations.prepareTexture(cache, std::move(value));
            }
            else if constexpr (std::is_same_v<T, asset::AssetSnapshot<asset::MeshAsset>>)
            {
                return preparations.prepareMesh(cache, std::move(value));
            }
            else if constexpr (std::is_same_v<T, asset::AssetSnapshot<asset::ShaderAsset>>)
            {
                return preparations.prepareShader(cache, std::move(value));
            }
            else if constexpr (std::is_same_v<T, asset::AssetSnapshot<asset::ShaderProgramAsset>>)
            {
                return preparations.prepareShaderProgram(cache, std::move(value));
            }
            else if constexpr (std::is_same_v<T,
                                              asset::AssetSnapshot<asset::MaterialTemplateAsset>>)
            {
                return preparations.prepareMaterialTemplate(cache, std::move(value));
            }
            else if constexpr (std::is_same_v<T, asset::AssetSnapshot<asset::MaterialAsset>>)
            {
                return preparations.prepareMaterial(cache, std::move(value));
            }
            else
            {
                return preparations.prepareModel(cache, std::move(value));
            }
        },
        std::move(source));
}
void finish(VulkanResourcePreparation& preparations, VulkanUploadService& uploads,
            ResourcePreparationResult result, bool pumpUploads = true)
{
    if (!result.accepted())
    {
        throw std::runtime_error("preparation rejected: " + result.error);
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (pumpUploads)
        {
            uploads.drain();
        }
        preparations.advance();
        const auto status = preparations.status(result.ticket);
        if (status.state == ResourcePreparationState::Ready)
        {
            return;
        }
        if (resourcePreparationFinished(status.state))
        {
            throw std::runtime_error("preparation failed: " + status.error);
        }
    }
    throw std::runtime_error("asset dependency preparation timed out");
}
} // namespace
void runAssetPreparationTests(const Device& device, asset::AssetManager& assets,
                              asset::ModelAssetHandle model,
                              asset::ShaderProgramAssetHandle present)
{
    VulkanUploadService uploads(device);
    RenderAssetCache cache;
    // Two external subscriptions must leave room for the internal dependency chain.
    VulkanResourcePreparation preparations(device, uploads, 2);
    auto original = assets.snapshot(model);
    auto a = preparations.prepareModel(cache, original);
    auto b = preparations.prepareModel(cache, original);
    require(a.accepted() && b.accepted() &&
                b.disposition == ResourcePreparationDisposition::Shared &&
                a.ticket.value != b.ticket.value,
            "model subscribers did not share work");
    preparations.advance();
    preparations.cancel(a.ticket);
    preparations.release(a.ticket);
    finish(preparations, uploads, b);
    preparations.release(b.ticket);
    require(bool(cache.model(model)), "model did not publish after dependencies");

    std::array<bool, 7> seen{};
    asset::TextureAssetHandle texture;
    asset::MaterialAssetHandle material;
    asset::MeshAssetHandle mesh;
    asset::ShaderProgramAssetHandle program;
    std::function<void(const asset::AnyAssetSnapshot&)> check;
    check = [&](const asset::AnyAssetSnapshot& snapshot)
    {
        seen[snapshot.index()] = true;
        const auto hit = prepare(preparations, cache, snapshot);
        require(hit.accepted() && hit.disposition == ResourcePreparationDisposition::CacheHit &&
                    preparations.status(hit.ticket).state == ResourcePreparationState::Ready,
                "a prepared dependency did not hit its versioned cache");
        preparations.release(hit.ticket);
        std::visit(
            [&](const auto& source)
            {
                using T = std::decay_t<decltype(source)>;
                if constexpr (std::is_same_v<T, asset::AssetSnapshot<asset::TextureAsset>>)
                {
                    texture = source.version.handle;
                }
                if constexpr (std::is_same_v<T, asset::AssetSnapshot<asset::MeshAsset>>)
                {
                    mesh = source.version.handle;
                }
                if constexpr (std::is_same_v<T, asset::AssetSnapshot<asset::MaterialAsset>>)
                {
                    material = source.version.handle;
                }
                if constexpr (std::is_same_v<T, asset::AssetSnapshot<asset::ShaderProgramAsset>>)
                {
                    program = source.version.handle;
                }
                if (source.dependencies)
                {
                    for (const auto& child : source.dependencies->direct)
                    {
                        check(child);
                    }
                }
            },
            snapshot);
    };
    check(original);
    for (bool value : seen)
    {
        require(value, "fixture did not cover every asset type");
    }
    require(cache.tryMaterial(material) && cache.shaderProgram(program),
            "prepared GPU objects are missing");

    const auto oldView = cache.texture(texture).view();
    const auto oldVertices = cache.mesh(mesh).vertexBuffer();
    const auto oldModel = cache.model(model);
    const auto oldProgram = cache.shaderProgram(program);
    const auto& previous = assets.texture(texture);
    asset::TextureAsset::CreateInfo replacement{
        previous.name(),       previous.width(),   previous.height(),  previous.format(),
        previous.colorSpace(), previous.sampler(), previous.payload(), previous.mipLevels()};
    auto discarded = assets.replaceTexture(texture, asset::TextureAsset(std::move(replacement)));
    auto updated = assets.snapshot(model);
    require(updated.version.contentRevision == original.version.contentRevision,
            "dependency edit unexpectedly changed model's own revision");
    const auto refresh = preparations.prepareModel(cache, updated);
    require(refresh.accepted() && refresh.disposition == ResourcePreparationDisposition::Started &&
                cache.model(model) == oldModel && cache.texture(texture).view() == oldView,
            "dependency update was reused or published prematurely");
    finish(preparations, uploads, refresh);
    preparations.release(refresh.ticket);
    require(cache.model(model) != oldModel && cache.texture(texture).view() != oldView &&
                cache.shaderProgram(program) == oldProgram,
            "dependency refresh failed replacement or rebuilt unchanged program");
    require(!preparations.prepareModel(cache, original).accepted(),
            "stale dependency snapshot was accepted");
    check(updated);
    require(cache.mesh(mesh).vertexBuffer() == oldVertices,
            "dependency update unnecessarily re-uploaded unchanged geometry");

    // A cancelled parent must release its internal tickets, even with only one external slot.
    {
        RenderAssetCache cancelledCache;
        VulkanResourcePreparation single(device, uploads, 1);
        auto root = single.prepareModel(cancelledCache, updated);
        require(root.accepted(), "single-slot model rejected");
        bool submitted = false;
        for (int step = 0; step < 1000 && !submitted; ++step)
        {
            single.advance();
            uploads.tick();
            submitted = uploads.stagedBytes() != 0;
        }
        require(submitted, "parent cancellation did not reach an in-flight dependency");
        single.cancel(root.ticket);
        single.release(root.ticket);
        require(!cancelledCache.model(model), "cancelled model was published");
        root = single.prepareModel(cancelledCache, updated);
        finish(single, uploads, root);
        single.release(root.ticket);
    }

    // A stale dependency fails its parent without publishing a partly prepared model.
    {
        RenderAssetCache failedCache;
        VulkanResourcePreparation failures(device, uploads, 2);
        const auto currentTexture = failures.prepareTexture(failedCache, assets.snapshot(texture));
        finish(failures, uploads, currentTexture);
        failures.release(currentTexture.ticket);
        const auto staleModel = failures.prepareModel(failedCache, original);
        require(staleModel.accepted(), "stale dependency test did not admit its parent");
        const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (!resourcePreparationFinished(failures.status(staleModel.ticket).state) &&
               std::chrono::steady_clock::now() < limit)
        {
            uploads.drain();
            failures.advance();
        }
        require(failures.status(staleModel.ticket).state == ResourcePreparationState::Failed &&
                    !failures.status(staleModel.ticket).error.empty() && !failedCache.model(model),
                "dependency failure did not propagate to the model");
        failures.release(staleModel.ticket);
        require(failures.empty(), "parent failure leaked internal subscription tickets");
    }

    // No-transfer preparation must work even while UploadService has no free request slots.
    {
        UploadLimits limits;
        limits.maxRequests = 1;
        VulkanUploadService fullUploads(device, limits);
        auto buffer = std::make_shared<Buffer>(device, 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        auto bytes = std::make_shared<std::array<std::byte, 4>>();
        BufferUpload op;
        op.destination = buffer;
        op.source = {bytes, bytes->data(), bytes->size()};
        op.finalStages = VK_PIPELINE_STAGE_TRANSFER_BIT;
        op.finalAccess = VK_ACCESS_TRANSFER_WRITE_BIT;
        UploadRequest request{{op}};
        const auto blocker = fullUploads.tryEnqueue(request);
        require(blocker.accepted(), "upload capacity blocker rejected");
        RenderAssetCache noUploadCache;
        VulkanResourcePreparation noUpload(device, fullUploads, 1);
        const auto result = noUpload.prepareShaderProgram(noUploadCache, assets.snapshot(present));
        finish(noUpload, fullUploads, result, false);
        require(fullUploads.query(blocker.ticket).state == UploadState::Queued &&
                    noUpload.status(result.ticket).totalBytes == 0,
                "shader program unexpectedly required transfer work");
        noUpload.release(result.ticket);

        VkAttachmentDescription color{};
        color.format = VK_FORMAT_R8G8B8A8_UNORM;
        color.samples = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkAttachmentReference reference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &reference;
        VkRenderPassCreateInfo passInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        passInfo.attachmentCount = 1;
        passInfo.pAttachments = &color;
        passInfo.subpassCount = 1;
        passInfo.pSubpasses = &subpass;
        RenderPass pass(device.get(), passInfo);
        auto pipelineInfo = makeDefaultPresentPipeline(noUploadCache.shaderProgram(present));
        pipelineInfo.renderPass = pass.get();
        pipelineInfo.program = noUploadCache.shaderProgram(present);
        pipelineInfo.descriptorSetLayouts = pipelineInfo.program->setLayouts();
        pipelineInfo.vertexShaderSpirv.clear();
        pipelineInfo.fragmentShaderSpirv.clear();
        GraphicsPipeline pipeline(device, pipelineInfo);
        require(bool(pipeline), "prepared modules could not create a graphics pipeline");
        fullUploads.cancel(blocker.ticket);
        fullUploads.releaseTicket(blocker.ticket);
    }
    device.waitIdle();
}
} // namespace rubia::test
