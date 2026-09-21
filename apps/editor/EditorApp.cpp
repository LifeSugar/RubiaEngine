#include "EditorApp.hpp"

#include "AppSmokeTests.hpp"

namespace rubia::editor
{

void EditorApp::run()
{
    App::RunConfig config = makeRunConfig();
    app_.run(config, editorLayer_);
}

void EditorApp::runRenderTest()
{
    rubia::test::AppSmokeTests::runRenderTest(
        app_,
        makeRunConfig(),
        editorLayer_);
}

App::RunConfig EditorApp::makeRunConfig()
{
    App::RunConfig config{};
    config.windowWidth = 1600;
    config.windowHeight = 900;
    config.windowTitle = "Vulkan Editor";
    config.enableDocking = true;
    config.initializeEmptyScene = true;
    config.imguiIniFilename = "editor_imgui.ini";
    config.outputMode = rhi::vulkan::VulkanRenderer::OutputMode::Editor;
    config.demoContent = DemoContentLoader::CreateInfo{
        "ABeautifulGame_extracted/ABeautifulGame.gltf",
        "shaders/triangle.vert.spv",
        "shaders/triangle.frag.spv",
        "shaders/present.vert.spv",
        "shaders/present.frag.spv",
        "assets",
        DemoContentLoader::TextureImportPolicy{
            importer::texture::KtxPayloadEncoding::Uastc,
            false,
            asset::TextureFormat::BC7UNorm,
            asset::TextureFormat::BC7UNorm,
            asset::TextureFormat::BC5UNorm,
            true}};
    return config;
}

} // namespace rubia::editor
