#pragma once

#include "App.hpp"

namespace rubia::editor
{
class ApplicationGui;
class VulkanApplicationBackend;
class App;
}

namespace rubia::test
{

/// Integration checks invoked only by the dedicated test executable.
class AppSmokeTests final
{
public:
    static editor::VulkanApplicationBackend& vulkanBackend(editor::App&);
    static void runAssetImportTest();
    static void runStartupTest();
    static void runEditorAssetTest();
    static void runPreparationGalleryTest();
    static void runRenderTest();
    static void runRenderTest(
        editor::App& app,
        const editor::App::RunConfig& config,
        editor::ApplicationGui& gui);
};

} // namespace rubia::test
