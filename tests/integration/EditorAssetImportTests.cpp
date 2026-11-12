#include "AppSmokeTests.hpp"
#include "backend/VulkanApplicationBackend.hpp"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "EditorLayer.hpp"
#include "asset/AssetId.hpp"
#include "content/EditorAssetController.hpp"
#include "render/SceneRenderExtractor.hpp"
#include "shader/HlslShaderCompiler.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <imgui.h>
#include <iostream>
#include <thread>

namespace rubia::test
{
void runEditorAssetImportTests(const editor::ApplicationGuiContext &context,
                               const std::function<void()> &renderFrame, const char* shaderFile)
{
    const auto require = [](bool value, const char *message) {
        if (!value)
            throw std::runtime_error(message);
    };
    editor::EditorAssetController controller;
    editor::EditorSelection selection;
    controller.update(context, selection);
    auto oldScene = context.scene;
    struct Restore
    {
        scene::Scene &scene;
        scene::Scene previous;
        ~Restore()
        {
            scene = std::move(previous);
        }
    } restore{context.scene, oldScene};
    const auto directory =
        std::filesystem::temp_directory_path() /
        std::filesystem::u8path(u8"rubia 资产 import " + asset::AssetId::generate().toString());
    require(std::filesystem::create_directory(directory),
            "could not create external import fixture");
    struct Cleanup
    {
        std::filesystem::path path;
        ~Cleanup()
        {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    } cleanup{directory};
    const auto source = std::filesystem::path(PROJECT_SOURCE_DIR) / "assets";
    for (const auto *file : {shaderFile, "RenderData.hlsli"})
        std::filesystem::copy_file(source / "shaders" / file, directory / file);
    std::filesystem::copy_file(source / "Models/Suzanne.glb", directory / "model with spaces.glb");
    std::filesystem::copy_file(source / "DamagedHelmet_extracted/baseColor_1.jpg",
                               directory / "albedo.jpg");
    const auto pump = [&] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (controller.busy() && std::chrono::steady_clock::now() < deadline)
        {
            context.preparations->advance();
            controller.update(context, selection);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        require(!controller.busy(), "editor import/preparation timed out");
    };
    controller.browse(directory);
    const auto browseDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (controller.directory() != directory && std::chrono::steady_clock::now() < browseDeadline)
    {
        controller.update(context, selection);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(controller.files().size() == 3,
            "external browser did not list GLB, shaders and texture");

    const auto shaderCount = context.assets.shaderHandles().size();
    editor::EditorAssetController::ImportOptions options;
    options.entryPoint = "VSMain";
    controller.importFile(directory / shaderFile, options);
    options.stage = asset::ShaderStage::Fragment;
    options.entryPoint = "PSMain";
    controller.importFile(directory / shaderFile, options);
    pump();
    const auto shaders = context.assets.shaderHandles();
    require(shaders.size() == shaderCount + 2, "external source shader import failed");
    const auto vertex = shaders[shaderCount], fragment = shaders[shaderCount + 1];
    require(context.assets.shader(vertex).stage() == asset::ShaderStage::Vertex &&
                context.assets.shader(fragment).stage() == asset::ShaderStage::Fragment,
            "Shader stages were not retained");
    controller.buildProgram(vertex, fragment);
    pump();
    require(context.assets.contains(controller.draftProgram()),
            "Compiled shader program did not reflect");

    controller.importFile(directory / "albedo.jpg", {});
    pump();
    const auto texture = std::get<asset::TextureAssetHandle>(selection.target());
    require(context.assets.texture(texture).colorSpace() == asset::TextureColorSpace::Srgb,
            "standalone texture lost import color space");
    const auto texturesBeforeRepeat = context.assets.textureHandles().size();
    controller.importFile(directory / "albedo.jpg", {});
    pump();
    require(context.assets.textureHandles().size() == texturesBeforeRepeat,
            "identical texture imports were duplicated");

    asset::MaterialTemplateAsset::CreateInfo layout;
    layout.name = "External Shader Material";
    layout.program = controller.draftProgram();
    layout.textureSlots = {{"colorTexture", "colorTexture", "colorSampler"}};
    controller.buildTemplate(layout);
    pump();
    require(context.assets.contains(controller.draftTemplate()),
            "material template generation failed");
    asset::MaterialAsset::CreateInfo material;
    material.name = "External material";
    material.materialTemplate = controller.draftTemplate();
    material.parameters = {{"tint", glm::vec4(1)}};
    for (const auto &slot : layout.textureSlots)
        material.textures.push_back({slot.name, texture});
    controller.createMaterial(material);
    pump();
    require(controller.message() == "Ready", "external material or PSO preparation failed");
    const auto customMaterial = std::get<asset::MaterialAssetHandle>(selection.target());

    const auto originalCount = context.scene.nodes().size();
    controller.importFile(directory / "model with spaces.glb", {}, true);
    // CPU import/preparation must not publish a scene node on request submission.
    require(context.scene.nodes().size() == originalCount, "model inserted before preparation");
    pump();
    require(context.scene.nodes().size() == originalCount + 1, "external GLB instantiation failed");
    const auto firstNode = static_cast<uint32_t>(originalCount);
    const auto model = context.scene.nodes()[firstNode].model;
    const auto modelsBeforeRepeat = context.assets.modelHandles().size();
    controller.importFile(directory / "model with spaces.glb", {}, true);
    pump();
    require(context.scene.nodes().size() == originalCount + 2 &&
                context.scene.nodes().back().model == model &&
                context.assets.modelHandles().size() == modelsBeforeRepeat,
            "repeated model drop did not share imported assets");
    controller.assignMaterial(firstNode, customMaterial);
    pump();
    require(context.scene.nodes()[firstNode].materialOverride == customMaterial &&
                !context.scene.nodes().back().materialOverride,
            "material override leaked into shared model instances");
    const auto candidates = render::SceneRenderExtractor{}.extract(context.scene, context.assets);
    bool found = false;
    for (const auto &candidate : candidates)
        found |= candidate.material == customMaterial;
    require(found, "extractor ignored the instance material override");
    renderFrame(); // Real descriptor/pipeline selection and Vulkan command recording.

    std::filesystem::copy_file(directory / "model with spaces.glb", directory / "cancel.glb");
    controller.importFile(directory / "cancel.glb", {});
    pump();
    const auto pendingModel = std::get<asset::ModelAssetHandle>(selection.target());
    controller.instantiate(pendingModel);
    controller.update(context, selection); // Admitted but not yet published.
    controller.stop();
    context.preparations->advance();
    require(context.scene.nodes().size() == originalCount + 2, "cancelled instance was inserted");
    controller.update(context, selection);
    controller.assignMaterial(firstNode, {});
    pump();
    require(!context.scene.nodes()[firstNode].materialOverride, "restoring GLB materials failed");

    // A material can be CPU-valid yet incompatible with the scene pipeline.
    // Unlit intentionally has no AlphaClip specialization; preserve the active material on failure.
    material.renderState.alphaClipEnabled = true;
    controller.createMaterial(material);
    pump();
    require(controller.message().find("AlphaClip") != std::string::npos,
            "incompatible pipeline was not reported");
    const auto incompatible = context.assets.materialHandles().back();
    controller.assignMaterial(firstNode, incompatible);
    pump();
    require(!context.scene.nodes()[firstNode].materialOverride,
            "pipeline failure replaced the working instance material");

    const auto beforeIncludeFailure = context.assets.shaderHandles().size();
    {
        std::ofstream include(directory / "RenderData.hlsli", std::ios::app);
        include << "\nthis is not valid shader syntax;\n";
    }
    controller.importFile(directory / shaderFile, options);
    pump();
    require(context.assets.shaderHandles().size() == beforeIncludeFailure &&
                controller.message().find("compilation failed") != std::string::npos,
            "Source shader reimport reused a stale include or hid diagnostics");

    const auto invalid = directory / (std::string("broken") + std::filesystem::path(shaderFile).extension().string());
    {
        std::ofstream stream(invalid);
        stream << "this is not a shader;";
    }
    const auto beforeFailure = context.assets.shaderHandles().size();
    controller.importFile(invalid, options);
    pump();
    require(context.assets.shaderHandles().size() == beforeFailure &&
                controller.message().find("compilation failed") != std::string::npos &&
                context.scene.nodes().size() == originalCount + 2,
            "shader failure changed live scene or hid diagnostics");
    controller.stop();
    std::clog
        << "[Editor Assets] " << shaderFile << ": external browsing, compile/reflection, texture upload, material "
           "PSO, GLB reuse, instance override, cancellation and compiler failure passed\n";
}
} // namespace rubia::test

namespace rubia::test
{
void AppSmokeTests::runEditorAssetTest()
{
    editor::EditorLayer gui;
    editor::App app;
    editor::App::RunConfig config;
    config.outputMode = render::OutputMode::Editor;
    try
    {
        app.initWindow(config, false);
        app.initBackend(config);
        app.startEmptyScene(config);
        app.setupCamera();
        app.camera.setCullingFlags(render::CullingFlags::None);
        app.initImGui(config);
        app.guiRenderBridge.attach(app.backend_->guiHost(), app.renderAssets, app.imguiLayer.rendererBackend());
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (!app.backend_->sceneReady() && std::chrono::steady_clock::now() < deadline)
        {
            app.resourcePreparation().advance();
            app.updateContentLoading();
            if (app.contentLoadStatus_.state == editor::ContentLoadState::Failed)
                throw std::runtime_error(app.contentLoadStatus_.message);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (!app.backend_->sceneReady() || !app.scene.nodes().empty() ||
            !app.assetManager.modelHandles().empty())
            throw std::runtime_error(
                "empty editor bootstrap did not prepare defaults independently of a model");
        editor::ApplicationGuiContext context{app.assetManager,
                                              app.scene,
                                              app.guiRenderBridge,
                                              &app.textureImports,
                                              &app.contentLoadStatus_,
                                              &app.resourcePreparation(),
                                              app.demoContent.defaultMaterial};
        gui.attach(context);
        for (const char* shaderFile : {"editor_unlit.hlsl", "editor_unlit.slang"})
        {
            runEditorAssetImportTests(context, [&] {
                for (uint32_t attempt = 0; attempt < 32; ++attempt)
                {
                    app.window.pollEvents();
                    app.imguiLayer.beginFrame();
                    app.drawGui(gui);
                    const auto result = app.backend_->render(app.makeRenderFrame(), app.renderAssets,
                                                            app.imguiLayer.endFrame());
                    if (render::needsResize(result))
                        app.reconfigurePresentation(gui);
                    if (render::wasSubmitted(result)) return;
                }
                throw std::runtime_error("external asset frame did not submit after swapchain retries");
            }, shaderFile);
        }
        if (app.backend_) app.backend_->waitIdle();
        gui.detach();
        app.cleanup();
    }
    catch (...)
    {
        if (app.backend_) app.backend_->waitIdle();
        gui.detach();
        app.cleanup();
        throw;
    }
}
} // namespace rubia::test
