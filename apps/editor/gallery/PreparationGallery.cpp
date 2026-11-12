#include "PreparationGallery.hpp"
#include "GalleryContent.hpp"
#include "App.hpp"
#include "EditorLayer.hpp"
#include <chrono>
#include <thread>
#include <stdexcept>

namespace rubia::editor
{
namespace
{
void requireGallery(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error("Preparation gallery: " + message);
}
template<class Predicate> void awaitGallery(Predicate done)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (!done())
    {
        requireGallery(std::chrono::steady_clock::now() < deadline, "preparation timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
}
void PreparationGallery::run()
{
    using namespace asset;
    using namespace render;
    editor::EditorLayer gui;
    editor::App app;
    editor::App::RunConfig config;
    config.windowWidth = 1600; config.windowHeight = 900;
    config.windowTitle = "Rubia - Preparation Gallery - Material Atelier";
    config.outputMode = render::OutputMode::Editor;
    const auto folder = std::filesystem::path(PROJECT_SOURCE_DIR) / "assets/preparation_gallery";
    try
    {
        app.initWindow(config, true);
        app.initBackend(config);
        app.startEmptyScene(config);
        app.setupCamera();
        app.initImGui(config);
        app.guiRenderBridge.attach(app.backend_->guiHost(), app.renderAssets, app.imguiLayer.rendererBackend());
        awaitGallery([&] {
            app.resourcePreparation().advance(); app.updateContentLoading(); app.window.pollEvents();
            requireGallery(app.contentLoadStatus_.state != editor::ContentLoadState::Failed,
                           app.contentLoadStatus_.message);
            return app.backend_->sceneReady();
        });
        auto& assets = app.assetManager;
        auto& preparation = app.resourcePreparation();
        editor::ApplicationGuiContext context{assets, app.scene, app.guiRenderBridge,
            &app.textureImports, &app.contentLoadStatus_, &preparation, app.demoContent.defaultMaterial};
        gui.attach(context);
        const auto content = editor::loadGalleryContent(context, folder, [&] {
            app.window.pollEvents(); preparation.advance();
        });
        const auto& materials = content.materials;
        const auto& roots = content.roots;
        std::vector<ResourcePreparationTicket> tickets;
        for (const auto root : roots)
        {
            const auto result = preparation.prepare(assets, root);
            requireGallery(result.accepted(), result.error);
            tickets.push_back(result.ticket);
        }
        awaitGallery([&] {
            preparation.advance(); app.window.pollEvents();
            bool ready = true;
            for (const auto ticket : tickets)
            {
                const auto status = preparation.status(ticket);
                requireGallery(status.state != ResourcePreparationState::Failed, status.error);
                ready &= status.state == ResourcePreparationState::Ready;
            }
            return ready;
        });
        for (const auto ticket : tickets) preparation.release(ticket);
        const auto warm = preparation.prewarmPipelines({materials.begin(), materials.end()});
        requireGallery(warm.accepted(), warm.error);
        awaitGallery([&] {
            preparation.advance(); app.window.pollEvents();
            const auto status = preparation.pipelineStatus(warm.ticket);
            requireGallery(status.state != PipelinePreparationState::Failed, status.error);
            return status.state == PipelinePreparationState::Ready;
        });
        preparation.releasePipelines(warm.ticket);
        populateGalleryScene(app.scene, content);
        app.camera.setRotation(glm::vec3(-40, 0, 0));
        app.camera.setPosition(glm::vec3(0, 1.9f, 0) - app.camera.getForwardVector() * 27.0f);
        app.camera.setAspect(16.0f / 9.0f);
        app.backend_->guiHost().resizeViewport(1600, 900);
        app.mainLoop(gui);
        app.backend_->waitIdle();
        gui.detach(); app.cleanup();
    }
    catch (...)
    {
        if (app.backend_) app.backend_->waitIdle();
        gui.detach(); app.cleanup(); throw;
    }
}
}
