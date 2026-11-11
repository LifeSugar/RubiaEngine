#include "App.hpp"
#include "EditorApp.hpp"
#include "AppSmokeTests.hpp"

#include <array>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>

namespace
{
struct LaunchOption
{
    std::string_view argument;
    std::string_view label;
};

constexpr std::array<LaunchOption, 9> launchOptions{{
    {"--editor", "编辑器（空场景）"},
    {"--runtime", "Runtime（空场景）"},
    {"--preparation-gallery", "资源准备展示"},
    {"--asset-test", "资产导入测试"},
    {"--startup-test", "启动与资源生命周期测试"},
    {"--render-test", "Runtime 渲染测试"},
    {"--editor-test", "编辑器渲染测试"},
    {"--editor-assets-test", "编辑器资产工作流测试"},
    {"--preparation-gallery-test", "资源准备展示测试"},
}};

void printLaunchOptions()
{
    std::cout << "\n  RubiaEngine 启动菜单\n\n";
    for (std::size_t i = 0; i < launchOptions.size(); ++i)
        std::cout << "  " << i + 1 << ". " << launchOptions[i].label
                  << "  (" << launchOptions[i].argument << ")\n";
    std::cout << "  0. 退出\n\n";
}

std::string_view chooseLaunchOption()
{
    printLaunchOptions();
    for (std::string input;;)
    {
        std::cout << "请选择 [0-9，回车默认 1]: " << std::flush;
        if (!std::getline(std::cin, input))
        {
            std::cout << '\n';
            return {};
        }
        const auto first = input.find_first_not_of(" \t\r");
        input = first == std::string::npos ? "1" :
            input.substr(first, input.find_last_not_of(" \t\r") - first + 1);
        if (input == "0") return {};
        for (std::size_t i = 0; i < launchOptions.size(); ++i)
        {
            if (input == std::to_string(i + 1))
            {
                std::cout << "\n启动：" << launchOptions[i].label << "\n" << std::flush;
                return launchOptions[i].argument;
            }
        }
        std::cout << "无效选项，请输入 0 到 9。\n";
    }
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        const std::string_view option = argc == 1 ? chooseLaunchOption() : argv[1];
        if (option.empty()) return EXIT_SUCCESS;
        if (option == "--help" || option == "-h")
        {
            printLaunchOptions();
            std::cout << "无参数启动显示菜单；也可直接使用上述参数启动。\n";
            return EXIT_SUCCESS;
        }
        // Keep the previous empty-scene aliases for command-line callers.
        const bool emptyAlias = argc == 3 && option == "--editor" &&
            std::string_view(argv[2]) == "--empty";
        bool knownOption = option == "--empty";
        for (const auto& entry : launchOptions) knownOption |= option == entry.argument;
        if (!knownOption || (argc > 2 && !emptyAlias))
        {
            std::cerr << "无效启动参数。使用 --help 查看启动选项。\n";
            return EXIT_FAILURE;
        }

        if (option == "--preparation-gallery" || option == "--preparation-gallery-test")
        {
            rubia::test::AppSmokeTests::runPreparationGallery(
                option == "--preparation-gallery");
        }
        else if (option == "--asset-test")
        {
            rubia::test::AppSmokeTests::runAssetImportTest();
            std::cout << "[OK] Asset import test passed\n";
        }
        else if (option == "--startup-test")
        {
            rubia::test::AppSmokeTests::runStartupTest();
            std::cout << "[OK] Startup test passed\n";
        }
        else if (option == "--render-test")
        {
            rubia::test::AppSmokeTests::runRenderTest();
            std::cout << "[OK] Render test passed\n";
        }
        else if (option == "--editor-assets-test")
        {
            rubia::test::AppSmokeTests::runEditorAssetTest();
            std::cout << "[OK] Editor asset workflow test passed\n";
        }
        else if (option == "--editor-test")
        {
            rubia::editor::EditorApp editor;
            editor.runRenderTest();
            std::cout << "[OK] Editor render test passed\n";
        }
        else if (option == "--editor")
        {
            rubia::editor::EditorApp editor;
            editor.run();
        }
        else
        {
            rubia::editor::App app;
            app.run();
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "[FATAL] " << e.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
