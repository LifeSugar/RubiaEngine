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

void AppSmokeTests::runStartupTest()
{
    using render::ScenePreparationState;
    // Keep log capture alive until App has joined its worker and drained uploads.
    editor::EditorLayer gui;
    editor::App app;
    editor::App::RunConfig config{};
    config.outputMode = rhi::vulkan::VulkanRenderer::OutputMode::Editor;
    config.autoLoadDemo = false;
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
            { return app.renderer.prepareTexture(app.renderAssets, sources->snapshot(handle)); };
            auto a = prepare(first);
            auto b = prepare(second);
            asset::MeshAsset::CreateInfo meshInfo;
            meshInfo.vertices.resize(3);
            meshInfo.indices = {0, 1, 2};
            meshInfo.submeshes.push_back({0, 3, 0, 3, {}});
            const auto meshHandle = sources->createMesh(std::move(meshInfo));
            const auto mesh =
                app.renderer.prepareMesh(app.renderAssets, sources->snapshot(meshHandle));
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
                app.updateContentLoading();
                if (app.renderer.resourcePreparationStatus(a.ticket).state ==
                        rhi::vulkan::ResourcePreparationState::Ready &&
                    app.renderer.resourcePreparationStatus(b.ticket).state ==
                        rhi::vulkan::ResourcePreparationState::Ready &&
                    app.renderer.resourcePreparationStatus(mesh.ticket).state ==
                        rhi::vulkan::ResourcePreparationState::Ready)
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
            app.renderer.releaseResourcePreparation(mesh.ticket);
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
            app.renderer.releaseResourcePreparation(a.ticket);
            app.renderer.releaseResourcePreparation(b.ticket);
            info.name = "Cancelled standalone preview";
            auto third = sources->createTexture(info);
            auto c = prepare(third);
            app.renderer.cancelResourcePreparation(c.ticket);
            app.renderer.releaseResourcePreparation(c.ticket);
            app.updateContentLoading();
            if (app.renderAssets.tryTexture(third) || app.renderAssets.texture(first).view() != firstView)
                throw std::runtime_error("standalone cancellation damaged the cache");
            // A synchronous upload drains shared work, but publication still belongs
            // to the renderer pump. Cancelling in this interval must never publish
            // or dereference a released texture; another completed request survives.
            auto fourth = sources->createTexture(info);
            auto fifth = sources->createTexture(info);
            auto d = prepare(fourth);
            auto e = prepare(fifth);
            if (!d.accepted() || !e.accepted())
            {
                throw std::runtime_error("completion cancellation test enqueue failed");
            }
            auto unpublished = app.renderer.uploadTextureAndWait(
                std::shared_ptr<const asset::TextureAsset>(sources, &sources->texture(first)));
            if (!unpublished || app.renderAssets.tryTexture(fourth) ||
                app.renderer.resourcePreparationStatus(d.ticket).state !=
                    rhi::vulkan::ResourcePreparationState::Uploading)
            {
                throw std::runtime_error("drain published standalone texture prematurely");
            }
            app.renderer.cancelResourcePreparation(d.ticket);
            if (app.renderer.resourcePreparationStatus(d.ticket).state !=
                rhi::vulkan::ResourcePreparationState::Cancelled)
            {
                throw std::runtime_error("completed but unpublished upload did not cancel");
            }
            // Keep the cancelled record for a pump to cover accidental re-publication.
            app.updateContentLoading();
            if (app.renderAssets.tryTexture(fourth) || !app.renderAssets.tryTexture(fifth) ||
                app.renderer.resourcePreparationStatus(e.ticket).state !=
                    rhi::vulkan::ResourcePreparationState::Ready)
            {
                throw std::runtime_error("completion cancellation damaged another texture");
            }
            app.renderer.releaseResourcePreparation(d.ticket);
            app.renderer.releaseResourcePreparation(e.ticket);
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

        app.renderer.beginScenePreparation(app.renderAssets, {});
        if (app.renderer.scenePreparationStatus().state != ScenePreparationState::Failed ||
            app.renderer.scenePreparationStatus().error.empty() || !app.renderer)
        {
            throw std::runtime_error("invalid request did not return a recoverable failure");
        }

        const auto makeRequest = [&]
        {
            render::SceneResourceRequest request;
            request.assets = std::shared_ptr<const asset::AssetManager>(prepared, &prepared->assets);
            request.models = {prepared->content.model};
            request.materialTemplate = prepared->content.materialTemplate;
            request.presentProgram = prepared->content.presentProgram;
            return request;
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

        // The backend retains the source even when the caller releases it.
        std::weak_ptr<editor::App::PreparedContent> source = prepared;
        app.renderer.beginScenePreparation(app.renderAssets, makeRequest());
        prepared.reset();
        if (source.expired())
            throw std::runtime_error("backend did not retain the CPU upload source");
        app.renderer.advanceResourcePreparation();
        const auto closing = app.renderer.scenePreparationStatus();
        if (closing.state != ScenePreparationState::PreparingResources || closing.total <= closing.completed)
            throw std::runtime_error("scene shutdown test did not restart resource preparation");
        // Exercise the close path while generic asset preparations are pending.
        glfwSetWindowShouldClose(app.window.nativeHandle(), GLFW_TRUE);
        app.mainLoop(gui);
        app.discardContentLoading();
        if (!source.expired() ||
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
