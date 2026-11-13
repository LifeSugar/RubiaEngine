#include "EditorApp.hpp"
#include "EditorBuildInfo.hpp"

namespace rubia::editor
{

void EditorApp::run()
{
    App::RunConfig config = makeRunConfig();
    app_.run(config, editorLayer_);
}

App::RunConfig EditorApp::makeRunConfig()
{
    App::RunConfig config{};
    config.windowWidth = 1600;
    config.windowHeight = 900;
    config.windowTitle = kEditorTitle;
    config.enableDocking = true;
    config.initializeEmptyScene = true;
    config.imguiIniFilename = "editor_imgui.ini";
    config.outputMode = render::OutputMode::Editor;
    config.demoContent = DemoContentLoader::CreateInfo{
        "ABeautifulGame_extracted/ABeautifulGame.gltf",
        "shaders/triangle.vert.hlsl",
        "shaders/triangle.frag.hlsl",
        "shaders/present.vert.hlsl",
        "shaders/present.frag.hlsl",
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
