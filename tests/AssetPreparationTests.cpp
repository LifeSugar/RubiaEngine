#include "vulkan/RetiredResources.hpp"
#include "VulkanUploadTests.hpp"
#include "render/SceneResourcePreparation.hpp"
#include "vulkan/DefaultPipelineFactory.hpp"
#include "vulkan/Device.hpp"
#include "vulkan/RenderAssetCache.hpp"
#include "vulkan/RenderPass.hpp"
#include "vulkan/VulkanResourcePreparation.hpp"
#include "vulkan/VulkanUploadService.hpp"
#include <array>
#include <algorithm>
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
                                  render::ResourceAssetSnapshot source)
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
    RetiredResources preparationsRetired;
    VulkanResourcePreparation preparations(device, uploads, preparationsRetired, {}, 2);
    const auto meshes = render::collectModelMeshes(assets, {model, model});
    require(!meshes.empty(), "model fixture contains no meshes");
    const auto rootMesh = meshes.back();
    auto original = assets.snapshot(rootMesh);
    auto a = preparations.prepareMesh(cache, original);
    auto b = preparations.prepareMesh(cache, original);
    require(a.accepted() && b.accepted() &&
                b.disposition == ResourcePreparationDisposition::Shared &&
                a.ticket.value != b.ticket.value,
            "mesh subscribers did not share work");
    preparations.advance();
    preparations.cancel(a.ticket);
    preparations.release(a.ticket);
    finish(preparations, uploads, b);
    preparations.release(b.ticket);
    require(cache.tryMesh(rootMesh), "mesh did not publish after dependencies");
    for (auto handle : meshes)
    {
        const auto request = preparations.prepareMesh(cache, assets.snapshot(handle));
        finish(preparations, uploads, request);
        preparations.release(request.ticket);
    }

    std::array<bool, 6> seen{};
    asset::TextureAssetHandle texture;
    asset::MaterialAssetHandle material;
    asset::MeshAssetHandle mesh;
    asset::ShaderProgramAssetHandle program;
    std::function<void(const render::ResourceAssetSnapshot&)> check;
    check = [&](const render::ResourceAssetSnapshot& snapshot)
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
                        check(render::resourceSnapshot(child));
                    }
                }
            },
            snapshot);
    };
    for (auto handle : meshes) check(assets.snapshot(handle));
    for (bool value : seen)
    {
        require(value, "fixture did not cover every asset type");
    }
    require(cache.tryMaterial(material) && cache.shaderProgram(program),
            "prepared GPU objects are missing");

    // The material builder describes an owned staging transfer, not a host map.
    // Destroying the unpublished material must not invalidate queued buffer copies.
    {
        auto snapshot = assets.snapshot(material);
        auto probe = makeMaterialPreparation(cache, snapshot);
        probe->createGpuResources(device);
        auto request = probe->buildUploadRequest();
        require(request.operations.size() == 1, "material must upload its parameter block");
        const auto& op = std::get<BufferUpload>(request.operations.front());
        require(op.destination && op.source.owner &&
                    op.destination->usage() == (VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT) &&
                    op.destination->size() == snapshot.data->parameterData().size() &&
                    op.source.size == snapshot.data->parameterData().size() &&
                    std::equal(snapshot.data->parameterData().begin(), snapshot.data->parameterData().end(), op.source.data) &&
                    op.finalAccess == VK_ACCESS_UNIFORM_READ_BIT && op.finalStages != 0,
                "material staging request lost its bytes, destination, or uniform-read barrier");
        std::weak_ptr<const Buffer> destination = op.destination;
        probe.reset();
        require(!destination.expired(), "unpublished material owned the only upload destination reference");
        const auto upload = uploads.tryEnqueue(request);
        require(upload.accepted(), "material parameter transfer was rejected");
        request.operations.clear();
        require(!destination.expired(), "queued material upload lost its destination");
        uploads.drain();
        require(uploads.query(upload.ticket).state == UploadState::Completed,
                "material parameter transfer did not complete");
        uploads.releaseTicket(upload.ticket);
        require(destination.expired(), "completed material transfer leaked its destination");
    }

    // Direct writes must need no upload capacity, including the combined
    // device-local/host-visible policy when the hardware supports that memory type.
    for (const auto policy : {render::MaterialParameterMemory::HostVisible,
                             render::MaterialParameterMemory::DeviceLocal | render::MaterialParameterMemory::HostVisible})
    {
        VkMemoryPropertyFlags required = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        if (render::hasMemoryProperty(policy, render::MaterialParameterMemory::DeviceLocal))
            required |= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        VkBufferCreateInfo probeInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        probeInfo.size = assets.material(material).parameterData().size();
        probeInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        VkBuffer probe = VK_NULL_HANDLE;
        require(vkCreateBuffer(device.get(), &probeInfo, nullptr, &probe) == VK_SUCCESS,
                "could not query material memory support");
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device.get(), probe, &requirements);
        vkDestroyBuffer(device.get(), probe, nullptr);
        VkPhysicalDeviceMemoryProperties memory{};
        vkGetPhysicalDeviceMemoryProperties(device.physical(), &memory);
        bool supported = false;
        for (uint32_t i = 0; i < memory.memoryTypeCount; ++i)
            supported |= (requirements.memoryTypeBits & (1u << i)) &&
                         (memory.memoryTypes[i].propertyFlags & required) == required;

        UploadLimits limits;
        limits.maxRequests = 1;
        VulkanUploadService directUploads(device, limits);
        RenderAssetCache directCache;
        RetiredResources directRetired;
        render::ResourcePreparationOptions options;
        options.material.parameterMemory = policy;
        VulkanResourcePreparation direct(device, directUploads, directRetired, options);
        auto snapshot = assets.snapshot(material);
        for (const auto& dependency : snapshot.dependencies->direct)
        {
            auto prepared = prepare(direct, directCache, render::resourceSnapshot(dependency));
            finish(direct, directUploads, prepared);
            direct.release(prepared.ticket);
        }
        auto blockerBuffer = std::make_shared<Buffer>(device, 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        auto bytes = std::make_shared<std::array<std::byte, 4>>();
        BufferUpload op;
        op.destination = blockerBuffer;
        op.source = {bytes, bytes->data(), bytes->size()};
        op.finalStages = VK_PIPELINE_STAGE_TRANSFER_BIT;
        op.finalAccess = VK_ACCESS_TRANSFER_WRITE_BIT;
        UploadRequest blockerRequest{{op}};
        auto blocker = directUploads.tryEnqueue(blockerRequest);
        require(blocker.accepted(), "direct-write test did not fill the upload queue");
        auto result = direct.prepareMaterial(directCache, snapshot);
        require(result.accepted(), "mapped material preparation rejected");
        for (int i = 0; i < 8 && !resourcePreparationFinished(direct.status(result.ticket).state); ++i)
            direct.advance();
        if (supported)
        {
            require(direct.status(result.ticket).state == ResourcePreparationState::Ready &&
                        direct.status(result.ticket).totalBytes == 0 &&
                        directUploads.query(blocker.ticket).state == UploadState::Queued,
                    "direct material writes used upload capacity or did not publish");
            const auto& buffer = directCache.material(material).parameterBufferResource();
            require((buffer->memoryProperties() & required) == required &&
                        !(buffer->usage() & VK_BUFFER_USAGE_TRANSFER_DST_BIT),
                    "direct material allocation did not respect requested memory properties");
        }
        else
        {
            require(direct.status(result.ticket).state == ResourcePreparationState::Failed &&
                        !direct.status(result.ticket).error.empty() && !directCache.tryMaterial(material),
                    "unsupported combined material policy silently fell back");
        }
        direct.release(result.ticket);
        directUploads.cancel(blocker.ticket);
        directUploads.releaseTicket(blocker.ticket);
    }
    {
        render::ResourcePreparationOptions invalid;
        invalid.material.parameterMemory = static_cast<render::MaterialParameterMemory>(0);
        bool rejected = false;
        try { VulkanResourcePreparation bad(device, uploads, preparationsRetired, invalid); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "empty material memory policy was accepted");
        invalid.material.parameterMemory = static_cast<render::MaterialParameterMemory>(4);
        rejected = false;
        try { VulkanResourcePreparation bad(device, uploads, preparationsRetired, invalid); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "unknown material memory policy bits were accepted");
        // Buffer writes work at byte offsets, preserve unwritten data, and move
        // the actual allocation properties with the owner.
        Buffer buffer(device, 16, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        std::array<std::byte, 16> data{};
        buffer.write(data.data(), data.size());
        data[3] = std::byte{0x42};
        buffer.write(data.data() + 3, 1, 3);
        const auto properties = buffer.memoryProperties();
        Buffer moved(std::move(buffer));
        require(moved.memoryProperties() == properties && buffer.memoryProperties() == 0,
                "buffer move lost memory properties");
        auto* mapped = static_cast<const std::byte*>(moved.map());
        const bool equal = std::equal(data.begin(), data.end(), mapped);
        moved.unmap();
        require(equal, "mapped buffer write corrupted its range");
    }

    const auto oldView = cache.texture(texture).view();
    const auto oldVertices = cache.mesh(mesh).vertexBuffer();
    const auto oldProgram = cache.shaderProgram(program);
    struct BindingSnapshot
    {
        asset::MaterialAssetHandle handle;
        VkBuffer parameters;
        VkDescriptorSet descriptor;
    };
    std::vector<BindingSnapshot> bindings;
    for (const auto handle : assets.materialHandles())
    {
        const auto* gpu = cache.tryMaterial(handle);
        const auto& textures = assets.material(handle).textures();
        if (gpu && std::find(textures.begin(), textures.end(), texture) != textures.end())
            bindings.push_back({handle, gpu->parameterBuffer(), gpu->descriptorSet()});
    }
    require(!bindings.empty(), "texture replacement fixture has no material users");
    // Accidentally passing a published set must be rejected before any write.
    bool rejectedInPlace = false;
    const auto& bound = cache.material(bindings.front().handle);
    try
    {
        static_cast<void>(bound.withTextures(device,
            cache.materialTemplate(bound.materialTemplate())->source(), {}, bound.descriptorSet()));
    }
    catch (const std::invalid_argument&)
    {
        rejectedInPlace = true;
    }
    require(rejectedInPlace, "texture rebinding accepted an already published descriptor");
    const auto& previous = assets.texture(texture);
    asset::TextureAsset::CreateInfo replacement{
        previous.name(),       previous.width(),   previous.height(),  previous.format(),
        previous.colorSpace(), previous.sampler(), previous.payload(), previous.mipLevels()};
    auto discarded = assets.replaceTexture(texture, asset::TextureAsset(std::move(replacement)));
    auto updated = assets.snapshot(rootMesh);
    require(updated.version.contentRevision == original.version.contentRevision,
            "dependency edit unexpectedly changed mesh's own revision");
    const auto refresh = preparations.prepareMesh(cache, updated);
    require(refresh.accepted() && refresh.disposition == ResourcePreparationDisposition::Started &&
                cache.texture(texture).view() == oldView,
            "dependency update was reused or published prematurely");
    finish(preparations, uploads, refresh);
    preparations.release(refresh.ticket);
    require(cache.texture(texture).view() != oldView &&
                cache.shaderProgram(program) == oldProgram,
            "dependency refresh failed replacement or rebuilt unchanged program");
    require(!preparations.prepareMesh(cache, original).accepted(),
            "stale dependency snapshot was accepted");
    check(updated);
    require(cache.mesh(mesh).vertexBuffer() == oldVertices,
            "dependency update unnecessarily re-uploaded unchanged geometry");
    for (const auto& binding : bindings)
    {
        const auto& gpu = cache.material(binding.handle);
        require(gpu.parameterBuffer() == binding.parameters &&
                    gpu.descriptorSet() != binding.descriptor,
                "texture dependency refresh reallocated parameters or rewrote an old descriptor");
    }
    require(!preparationsRetired.empty(),
            "texture/material replacement did not retain old resource owners");

    // A cancelled parent must release its internal tickets, even with only one external slot.
    {
        RenderAssetCache cancelledCache;
        RetiredResources singleRetired;
        VulkanResourcePreparation single(device, uploads, singleRetired, {}, 1);
        auto root = single.prepareMesh(cancelledCache, updated);
        require(root.accepted(), "single-slot mesh rejected");
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
        require(!cancelledCache.tryMesh(rootMesh), "cancelled mesh was published");
        root = single.prepareMesh(cancelledCache, updated);
        finish(single, uploads, root);
        single.release(root.ticket);
    }

    // A stale dependency fails its parent without publishing a partly prepared mesh.
    {
        RenderAssetCache failedCache;
        RetiredResources failuresRetired;
        VulkanResourcePreparation failures(device, uploads, failuresRetired, {}, 2);
        const auto currentTexture = failures.prepareTexture(failedCache, assets.snapshot(texture));
        finish(failures, uploads, currentTexture);
        failures.release(currentTexture.ticket);
        const auto staleMesh = failures.prepareMesh(failedCache, original);
        require(staleMesh.accepted(), "stale dependency test did not admit its parent");
        const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (!resourcePreparationFinished(failures.status(staleMesh.ticket).state) &&
               std::chrono::steady_clock::now() < limit)
        {
            uploads.drain();
            failures.advance();
        }
        require(failures.status(staleMesh.ticket).state == ResourcePreparationState::Failed &&
                    !failures.status(staleMesh.ticket).error.empty() && !failedCache.tryMesh(rootMesh),
                "dependency failure did not propagate to the mesh");
        failures.release(staleMesh.ticket);
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
        RetiredResources noUploadRetired;
        VulkanResourcePreparation noUpload(device, fullUploads, noUploadRetired, {}, 1);
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
    // Consecutive standalone texture updates must also share the same parameters.
    // Old binding batches remain alive until explicitly reclaimed by this test owner.
    for (int update = 0; update < 2; ++update)
    {
        for (auto& binding : bindings)
            binding.descriptor = cache.material(binding.handle).descriptorSet();
        const auto& current = assets.texture(texture);
        asset::TextureAsset::CreateInfo next{
            current.name(), current.width(), current.height(), current.format(),
            current.colorSpace(), current.sampler(), current.payload(), current.mipLevels()};
        static_cast<void>(assets.replaceTexture(texture, asset::TextureAsset(std::move(next))));
        const auto ticket = preparations.prepareTexture(cache, assets.snapshot(texture));
        finish(preparations, uploads, ticket);
        preparations.release(ticket.ticket);
        for (const auto& binding : bindings)
        {
            const auto& gpu = cache.material(binding.handle);
            require(gpu.parameterBuffer() == binding.parameters &&
                        gpu.descriptorSet() != binding.descriptor,
                    "consecutive texture replacements lost immutable parameter sharing");
        }
    }
    device.waitIdle();
    preparationsRetired.clear();
    for (const auto& binding : bindings)
        require(cache.material(binding.handle).parameterBuffer() == binding.parameters,
                "retiring old bindings released the current parameter buffer");
    // Exercise a newer material version with resident dependencies. Parameter
    // upload admission/completion must gate publication and preserve the old binding.
    {
        UploadLimits limits;
        limits.maxRequests = 1;
        VulkanUploadService materialUploads(device, limits);
        RetiredResources materialRetired;
        VulkanResourcePreparation materials(device, materialUploads, materialRetired);
        auto source = assets.snapshot(material);
        ++source.version.contentRevision; // Synthetic revision of the same immutable test bytes.
        const auto oldBuffer = cache.material(material).parameterBuffer();
        const auto oldSet = cache.material(material).descriptorSet();
        auto blockerBuffer = std::make_shared<Buffer>(device, 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        auto bytes = std::make_shared<std::array<std::byte, 4>>();
        BufferUpload blockerOp;
        blockerOp.destination = blockerBuffer;
        blockerOp.source = {bytes, bytes->data(), bytes->size()};
        blockerOp.finalStages = VK_PIPELINE_STAGE_TRANSFER_BIT;
        blockerOp.finalAccess = VK_ACCESS_TRANSFER_WRITE_BIT;
        UploadRequest blockerRequest{{blockerOp}};
        const auto blocker = materialUploads.tryEnqueue(blockerRequest);
        require(blocker.accepted(), "material queue blocker was rejected");
        auto request = materials.prepareMaterial(cache, source);
        require(request.accepted(), "material update rejected before upload admission");
        for (int i = 0; i < 4; ++i) materials.advance();
        require(materials.status(request.ticket).state == ResourcePreparationState::Preparing &&
                    cache.material(material).parameterBuffer() == oldBuffer &&
                    cache.material(material).descriptorSet() == oldSet,
                "queue-full material update published uninitialized parameters");
        materialUploads.cancel(blocker.ticket);
        materialUploads.releaseTicket(blocker.ticket);
        materials.advance();
        require(materials.status(request.ticket).totalBytes == source.data->parameterData().size(),
                "material did not retry its parameter upload after queue capacity recovered");
        materialUploads.drain();
        require(materials.status(request.ticket).state == ResourcePreparationState::Uploading &&
                    cache.material(material).parameterBuffer() == oldBuffer,
                "completed transfer bypassed material publication");
        materials.cancel(request.ticket); // Completed transfer, not yet published.
        materials.release(request.ticket);
        materials.advance();
        require(cache.material(material).parameterBuffer() == oldBuffer,
                "cancelled parameter upload replaced the old material");

        request = materials.prepareMaterial(cache, source);
        finish(materials, materialUploads, request);
        const auto ready = materials.status(request.ticket);
        require(ready.totalBytes == source.data->parameterData().size() &&
                    ready.completedBytes == ready.totalBytes && ready.submittedBytes == ready.totalBytes &&
                    cache.material(material).parameterBuffer() != oldBuffer &&
                    cache.material(material).descriptorSet() != oldSet && !materialRetired.empty(),
                "material update did not publish completed parameters and retire its old binding");
        materials.release(request.ticket);
        const auto hit = materials.prepareMaterial(cache, source);
        require(hit.accepted() && hit.disposition == ResourcePreparationDisposition::CacheHit,
                "published material revision was uploaded again");
        materials.release(hit.ticket);
        device.waitIdle();
        materialRetired.clear();
    }
}
} // namespace rubia::test
