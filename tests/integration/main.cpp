#include "AppSmokeTests.hpp"
#include "EditorApp.hpp"
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string_view>

int main(int argc, char** argv)
{
    using rubia::test::AppSmokeTests;
    const std::string_view option = argc == 2 ? argv[1] : "";
    try
    {
        if (option == "--asset-test") AppSmokeTests::runAssetImportTest();
        else if (option == "--startup-test") AppSmokeTests::runStartupTest();
        else if (option == "--render-test") AppSmokeTests::runRenderTest();
        else if (option == "--editor-assets-test") AppSmokeTests::runEditorAssetTest();
        else if (option == "--preparation-gallery-test") AppSmokeTests::runPreparationGalleryTest();
        else if (option == "--editor-test")
        {
            rubia::editor::EditorLayer gui;
            rubia::editor::App app;
            AppSmokeTests::runRenderTest(app, rubia::editor::EditorApp::makeRunConfig(), gui);
        }
        else
        {
            std::cerr << "Usage: RubiaIntegrationTests <test>\n"
                "  --asset-test | --startup-test | --render-test | --editor-test\n"
                "  --editor-assets-test | --preparation-gallery-test\n";
            return option == "--help" ? EXIT_SUCCESS : EXIT_FAILURE;
        }
        std::cout << "[OK] " << option << " passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
