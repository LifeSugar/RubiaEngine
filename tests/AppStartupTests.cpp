#include "AppSmokeTests.hpp"
#include "VulkanUploadTests.hpp"
#include <imgui.h>

#include "EditorLayer.hpp"
#include "render/SceneResourcePreparation.hpp"

#include <chrono>
#include <iostream>
#include <stdexcept>

namespace rubia::test
{

#ifdef RUBIA_PIPELINE_STATE_TESTS
void runGraphicsPipelineTests(const rhi::vulkan::Device& device);
void runVulkanDrawListTests(const rhi::vulkan::Device& device);
#endif

void AppSmokeTests::runStartupTest()
{
    using render::ScenePreparationState;
    // Keep log capture alive until App has joined its worker and drained uploads.
    editor::EditorLayer gui;
    editor::App app;
    editor::App::RunConfig config{};
    config.outputMode = rhi::vulkan::VulkanRenderer::OutputMode::Editor;
    const auto start = std::chrono::steady_clock::now();
    app.initWindow(config, false);
    app.initVulkan(config);
    app.setupCamera();
    app.initImGui(config);
    app.guiRenderBridge.attach(app.renderer, app.renderAssets);
    gui.attach({app.assetManager, app.scene, app.guiRenderBridge,
        &app.textureImports, &app.contentLoadStatus_});
    const auto draw = [&]
    {
        app.window.pollEvents();
        app.imguiLayer.beginFrame();
        app.drawGui(gui);
        if (app.renderer.renderGui(app.imguiLayer.endFrame()) ==
            rhi::vulkan::VulkanRenderer::RenderResult::NeedsResize)
        {
            app.recreateSwapChain(gui);
        }
    };
    try
    {
        draw();
        const auto firstGui = std::chrono::steady_clock::now();
        if (app.contentLoadFuture_.valid() || app.renderer.sceneReady())
        {
            throw std::runtime_error("empty startup unexpectedly started content loading");
        }
        app.recreateSwapChain(gui);
        draw();

        runVulkanUploadTests(app.vulkanContext.device());
#ifdef RUBIA_PIPELINE_STATE_TESTS
        runGraphicsPipelineTests(app.vulkanContext.device());
        runVulkanDrawListTests(app.vulkanContext.device());
#endif

        // Standalone incremental resource preparation: no model, material layout or pipeline.
        {
            auto sources = std::make_shared<asset::AssetManager>();
            asset::TextureAsset::CreateInfo info;
            info.name = "Standalone upload preview";
            info.width = info.height = 2;
            info.format = asset::TextureFormat::RGBA8UNorm;
            info.payload.assign(16, std::byte{0xff});
            auto first = sources->createTexture(info);
            info.name = "Second standalone preview";
            auto second = sources->createTexture(info);
            auto prepare = [&](asset::TextureAssetHandle handle)
            { return app.resourcePreparation().prepare(*sources, handle); };
            auto a = prepare(first);
            auto b = prepare(second);
            auto shared = prepare(first);
            if (!shared.accepted() || shared.ticket.value == a.ticket.value ||
                shared.disposition != render::ResourcePreparationDisposition::Shared)
                throw std::runtime_error("frontend requests did not share backend preparation");
            app.resourcePreparation().cancel(shared.ticket);
            if (app.resourcePreparation().status(shared.ticket).state !=
                render::ResourcePreparationState::Cancelled)
                throw std::runtime_error("frontend cancellation was not observable");
            app.resourcePreparation().release(shared.ticket);
            const auto invalid = app.resourcePreparation().prepare(*sources, asset::TextureAssetHandle{});
            if (invalid.accepted() || invalid.ticket ||
                invalid.code != render::ResourcePreparationCode::InvalidRequest)
                throw std::runtime_error("frontend accepted an invalid CPU handle");
            asset::MeshAsset::CreateInfo meshInfo;
            meshInfo.vertices.resize(3);
            meshInfo.indices = {0, 1, 2};
            meshInfo.submeshes.push_back({0, 3, 0, 3, {}});
            const auto meshHandle = sources->createMesh(std::move(meshInfo));
            const auto mesh =
                app.resourcePreparation().prepare(*sources, meshHandle);
            if (!mesh.accepted() || app.renderAssets.tryMesh(meshHandle))
            {
                throw std::runtime_error("standalone mesh failed admission or was published early");
            }
            if (!a.accepted() || !b.accepted() || app.renderAssets.tryTexture(first) || app.renderAssets.tryTexture(second))
                throw std::runtime_error("standalone upload exposed an unready texture");
            auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(15);
            while (std::chrono::steady_clock::now() < limit)
            {
                // This is the normal App pump even while content loading is Idle.
                app.resourcePreparation().advance();
                if (app.resourcePreparation().status(a.ticket).state ==
                        render::ResourcePreparationState::Ready &&
                    app.resourcePreparation().status(b.ticket).state ==
                        render::ResourcePreparationState::Ready &&
                    app.resourcePreparation().status(mesh.ticket).state ==
                        render::ResourcePreparationState::Ready)
                {
                    break;
                }
                draw();
            }
            if (!app.renderAssets.tryTexture(first) || !app.renderAssets.tryTexture(second) || app.renderer.sceneReady())
                throw std::runtime_error("standalone texture preparation required a scene or did not publish");
            if (!app.renderAssets.tryMesh(meshHandle))
            {
                throw std::runtime_error("standalone mesh was not published by the App pump");
            }
            app.resourcePreparation().release(mesh.ticket);
            const auto firstView = app.renderAssets.texture(first).view();
            auto preview = app.guiRenderBridge.preview(first);
            if (!preview) throw std::runtime_error("standalone upload has no GUI preview");
            app.imguiLayer.beginFrame();
            ImGui::Begin("Standalone texture upload test");
            ImGui::Image(ImTextureRef(static_cast<ImTextureID>(preview.textureId)), ImVec2(32, 32));
            ImGui::End();
            const auto previewResult = app.renderer.renderGui(app.imguiLayer.endFrame());
            if (previewResult == rhi::vulkan::VulkanRenderer::RenderResult::NeedsResize)
                app.recreateSwapChain(gui);
            app.resourcePreparation().release(a.ticket);
            app.resourcePreparation().release(b.ticket);
            auto hit = prepare(first);
            if (!hit.accepted() || hit.disposition != render::ResourcePreparationDisposition::CacheHit ||
                app.resourcePreparation().status(hit.ticket).state != render::ResourcePreparationState::Ready)
                throw std::runtime_error("frontend did not expose cache reuse");
            app.resourcePreparation().release(hit.ticket);
            info.name = "Cancelled standalone preview";
            auto third = sources->createTexture(info);
            auto c = prepare(third);
            app.resourcePreparation().cancel(c.ticket);
            app.resourcePreparation().release(c.ticket);
            app.resourcePreparation().advance();
            if (app.renderAssets.tryTexture(third) || app.renderAssets.texture(first).view() != firstView)
                throw std::runtime_error("standalone cancellation damaged the cache");
            // Cancel after creation dispatch. The old result must not publish or
            // damage another request that the normal renderer pump later completes.
            auto fourth = sources->createTexture(info);
            auto fifth = sources->createTexture(info);
            auto d = prepare(fourth);
            auto e = prepare(fifth);
            if (!d.accepted() || !e.accepted())
            {
                throw std::runtime_error("completion cancellation test enqueue failed");
            }
            app.resourcePreparation().advance();
            if (app.renderAssets.tryTexture(fourth) ||
                app.resourcePreparation().status(d.ticket).state != render::ResourcePreparationState::Preparing)
                throw std::runtime_error("creation dispatch published standalone texture prematurely");
            app.resourcePreparation().cancel(d.ticket);
            if (app.resourcePreparation().status(d.ticket).state !=
                render::ResourcePreparationState::Cancelled)
            {
                throw std::runtime_error("completed but unpublished upload did not cancel");
            }
            // Keep the cancelled record while pumping its surviving sibling.
            limit = std::chrono::steady_clock::now() + std::chrono::seconds(15);
            while (!render::resourcePreparationFinished(app.resourcePreparation().status(e.ticket).state) &&
                   std::chrono::steady_clock::now() < limit)
            {
                app.resourcePreparation().advance();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            if (app.renderAssets.tryTexture(fourth) || !app.renderAssets.tryTexture(fifth) ||
                app.resourcePreparation().status(e.ticket).state !=
                    render::ResourcePreparationState::Ready)
            {
                throw std::runtime_error("completion cancellation damaged another texture");
            }
            app.resourcePreparation().release(d.ticket);
            app.resourcePreparation().release(e.ticket);
            // Closing the frontend session cancels only its subscriptions and
            // keeps already resident resources. Released tickets become invalid.
            auto pendingHandle = sources->createTexture(info);
            auto pending = prepare(pendingHandle);
            app.resourcePreparation().cancelAll();
            bool released = false;
            try { static_cast<void>(app.resourcePreparation().status(pending.ticket)); }
            catch (const std::out_of_range&) { released = true; }
            app.resourcePreparation().advance();
            if (!released || app.renderAssets.tryTexture(pendingHandle) ||
                app.renderAssets.texture(first).view() != firstView)
                throw std::runtime_error("frontend shutdown leaked work or evicted resident resources");
            app.renderer.waitIdle();
            app.guiRenderBridge.invalidatePreview(first);
            app.renderAssets.reset();
        }

        // Cooperative cancellation also works before the worker gets scheduled.
        app.contentLoadConfig_ = config.demoContent;
        app.startContentLoading();
        app.discardContentLoading();
        if (app.contentLoadFuture_.valid() || app.preparedContent_ ||
            !app.assetManager.textureHandles().empty())
        {
            throw std::runtime_error("CPU cancellation left live content or a worker");
        }
        app.contentLoadStatus_ = {};

        // Valid builtins must not make a missing model fatal to the editor.
        app.contentLoadConfig_.modelPath = "__missing_startup_model__.gltf";
        app.startContentLoading();
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (app.contentLoadStatus_.state == editor::ContentLoadState::Preparing &&
            std::chrono::steady_clock::now() < deadline)
        {
            app.resourcePreparation().advance();
            app.updateContentLoading();
            draw();
        }
        if (app.contentLoadStatus_.state != editor::ContentLoadState::Failed ||
            !app.renderer || !app.scene.nodes().empty())
        {
            throw std::runtime_error("missing model did not leave a usable empty editor");
        }

        app.contentLoadConfig_ = config.demoContent;
        app.startContentLoading();
        deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
        while (std::chrono::steady_clock::now() < deadline)
        {
            app.resourcePreparation().advance();
            app.updateContentLoading();
            if (app.contentLoadStatus_.state == editor::ContentLoadState::Failed)
            {
                throw std::runtime_error(app.contentLoadStatus_.message);
            }
            draw();
            const auto status = app.renderer.scenePreparationStatus();
            if (status.state == ScenePreparationState::PreparingResources && status.total > status.completed)
            {
                break;
            }
        }
        const auto uploading = app.renderer.scenePreparationStatus();
        if (uploading.state != ScenePreparationState::PreparingResources || uploading.total <= uploading.completed)
        {
            throw std::runtime_error("scene cancellation test did not begin resource preparation");
        }
        // Keep the CPU source to exercise cancellation and restart independently
        // of the App worker. The backend must not clear an existing operation.
        auto prepared = app.preparedContent_;
        bool duplicateRejected = false;
        try { app.renderer.beginScenePreparation(app.renderAssets, {}); }
        catch (const std::logic_error&) { duplicateRejected = true; }
        if (!duplicateRejected ||
            app.renderer.scenePreparationStatus().total != uploading.total)
        {
            throw std::runtime_error("duplicate preparation replaced an in-flight operation");
        }
        app.discardContentLoading();
        if (app.renderer.scenePreparationStatus().state != ScenePreparationState::Cancelled ||
            !app.renderAssets.empty() ||
            app.renderer.sceneReady() || !app.assetManager.textureHandles().empty())
        {
            throw std::runtime_error("upload cancellation left partial scene resources");
        }
        draw();

        runAssetPreparationTests(app.vulkanContext.device(), prepared->assets,
                                 prepared->content.model, prepared->content.presentProgram);

        // Exercise the actual Renderer submission boundary with weak owners.
        // A prepared shader needs no upload and makes retirement observable
        // without exposing frame internals or relying on GPU timing.
        {
            rhi::vulkan::RenderAssetCache cache;
            const auto domain = std::make_shared<asset::AssetDomainTag>();
            const asset::ShaderAssetHandle handle{0, 1};
            const auto publish = [&](asset::AssetContentRevision revision)
            {
                const auto shader = prepared->assets.shaderProgram(prepared->content.presentProgram).shaders().front();
                auto owner = std::make_shared<asset::ShaderAsset>(prepared->assets.shader(shader));
                std::weak_ptr<const asset::ShaderAsset> weak = owner;
                const auto result = app.renderer.prepareShader(
                    cache, {domain, {handle, revision}, std::move(owner)});
                if (!result.accepted())
                    throw std::runtime_error("retirement probe preparation rejected");
                const auto creationDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
                while (!rhi::vulkan::resourcePreparationFinished(app.renderer.resourcePreparationStatus(result.ticket).state) &&
                       std::chrono::steady_clock::now() < creationDeadline)
                {
                    app.renderer.advanceResourcePreparation();
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                if (app.renderer.resourcePreparationStatus(result.ticket).state !=
                    rhi::vulkan::ResourcePreparationState::Ready)
                    throw std::runtime_error("retirement probe was not published");
                app.renderer.releaseResourcePreparation(result.ticket);
                return weak;
            };

            const auto first = publish(1);
            const auto second = publish(2);
            app.renderer.advanceResourcePreparation(); // No render submission yet.
            if (first.expired())
                throw std::runtime_error("unsubmitted retirement was released");

            // The first draw must not reclaim first using the slot's OLD fence.
            // Other slots completing must not reclaim it either.
            for (uint32_t i = 0; i < app.renderer.frameCount(); ++i)
            {
                draw();
                if (first.expired())
                    throw std::runtime_error("retirement used the wrong frame fence cycle");
            }
            draw(); // Reuse the slot whose NEW submission protects first.
            if (!first.expired() || second.expired())
                throw std::runtime_error("frame retirement lost the resident or retained the old owner");

            const auto third = publish(3);
            draw(); // second is now in a submitted slot.
            const auto fourth = publish(4); // third is still pending submission.
            if (second.expired() || third.expired())
                throw std::runtime_error("consecutive replacements retired prematurely");
            app.recreateSwapChain(gui); // Idle recovery must drain BOTH categories.
            if (!second.expired() || !third.expired() || fourth.expired())
                throw std::runtime_error("idle retirement failed to preserve current resources");

            // With no updates, subsequent frame reuse must leave the resident alone.
            for (uint32_t i = 0; i <= app.renderer.frameCount(); ++i)
                draw();
            if (fourth.expired())
                throw std::runtime_error("unchanged resident was retired");
        }

        app.renderer.beginScenePreparation(app.renderAssets, {});
        if (app.renderer.scenePreparationStatus().state != ScenePreparationState::Failed ||
            app.renderer.scenePreparationStatus().error.empty() || !app.renderer)
        {
            throw std::runtime_error("invalid request did not return a recoverable failure");
        }

        const auto makeRequest = [&]
        {
            return render::makeSceneResourceRequest(prepared->assets, {prepared->content.model},
                prepared->content.materialTemplate, prepared->content.presentProgram);
        };
        app.renderer.beginScenePreparation(app.renderAssets, makeRequest());
        deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
        while (app.renderer.scenePreparationStatus().state != ScenePreparationState::Ready &&
            std::chrono::steady_clock::now() < deadline)
        {
            app.renderer.advanceResourcePreparation();
            const auto status = app.renderer.scenePreparationStatus();
            if (status.state == ScenePreparationState::Failed)
                throw std::runtime_error(status.error);
            if (status.completed > status.total ||
                app.renderer.sceneReady())
                throw std::runtime_error("preparation exposed incomplete scene resources");
            draw();
        }
        const auto ready = app.renderer.scenePreparationStatus();
        if (ready.state != ScenePreparationState::Ready || ready.completed != ready.total)
            throw std::runtime_error("backend preparation did not finish all uploads and pipelines");
        app.recreateSwapChain(gui);
        draw();
        if (app.renderer.sceneReady())
            throw std::runtime_error("resize activated a prepared scene");
        app.renderer.cancelScenePreparation();
        if (!app.renderAssets.empty() || app.renderer.sceneReady())
            throw std::runtime_error("ready cancellation left scene resources alive");

        // The backend retains concrete snapshots without retaining the CPU scene bundle.
        std::weak_ptr<editor::App::PreparedContent> source = prepared;
        auto detachedRequest = makeRequest();
        std::weak_ptr<const asset::MeshAsset> meshSource = detachedRequest.meshes.front().data;
        app.renderer.beginScenePreparation(app.renderAssets, std::move(detachedRequest));
        prepared.reset();
        if (!source.expired() || meshSource.expired())
            throw std::runtime_error("backend retained CPU scene or lost concrete mesh snapshot");
        app.renderer.advanceResourcePreparation();
        const auto closing = app.renderer.scenePreparationStatus();
        if (closing.state != ScenePreparationState::PreparingResources || closing.total <= closing.completed)
            throw std::runtime_error("scene shutdown test did not restart resource preparation");
        // Exercise the close path while generic asset preparations are pending.
        glfwSetWindowShouldClose(app.window.nativeHandle(), GLFW_TRUE);
        app.mainLoop(gui);
        app.discardContentLoading();
        if (!source.expired() || !meshSource.expired() ||
            app.renderer.scenePreparationStatus().state != ScenePreparationState::Cancelled)
            throw std::runtime_error("cancellation retained the CPU upload source");
        app.renderer.waitIdle();
        gui.detach();
        app.cleanup();
        if (app.window || app.renderer ||
            app.renderer.scenePreparationStatus().state != ScenePreparationState::Idle ||
            app.contentLoadFuture_.valid())
        {
            throw std::runtime_error("startup shutdown left application resources alive");
        }
        std::clog << "[Startup] First GUI submission: "
            << std::chrono::duration_cast<std::chrono::milliseconds>(firstGui - start).count()
            << " ms; empty resize, CPU cancellation, missing model, preparation restart, ready cancellation and upload shutdown passed\n";
    }
    catch (...)
    {
        app.discardContentLoading();
        app.renderer.waitIdle();
        gui.detach();
        app.cleanup();
        throw;
    }
}

} // namespace rubia::test
