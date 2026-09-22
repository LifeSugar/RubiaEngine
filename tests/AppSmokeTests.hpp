#pragma once

#include "App.hpp"

namespace rubia::editor
{
class ApplicationGui;
class App;
}

namespace rubia::test
{

/// Existing command-line validation paths, kept outside the runtime App API.
class AppSmokeTests final
{
public:
    static void runAssetImportTest();
    static void runStartupTest();
    static void runEditorAssetTest();
    static void runPreparationGallery(bool interactive);
    static void runRenderTest();
    static void runRenderTest(
        editor::App& app,
        const editor::App::RunConfig& config,
        editor::ApplicationGui& gui);
};

} // namespace rubia::test
