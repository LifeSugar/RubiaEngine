#include "ImGuiLayer.hpp"
#include "EditorTheme.hpp"


#include <imgui.h>

#include <filesystem>
#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace rubia::editor
{
namespace
{

void loadEditorFont(ImGuiIO& io)
{
    constexpr float fontSize = 15.0f;
    ImFontConfig fontConfig;
    fontConfig.Flags |= ImFontFlags_NoLoadError;
    // Use a compact proportional UI face. Do not redistribute platform fonts.
    const char* windowsDirectory = std::getenv("WINDIR");
    const std::filesystem::path candidates[] = {
        windowsDirectory ? std::filesystem::path(windowsDirectory) / "Fonts/tahoma.ttf" : std::filesystem::path{},
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"};
    for (const auto& path : candidates)
    {
        std::error_code error;
        if (path.empty() || !std::filesystem::is_regular_file(path, error)) continue;
        if (ImFont* font = io.Fonts->AddFontFromFileTTF(path.u8string().c_str(), fontSize, &fontConfig))
        {
            io.FontDefault = font;
            break;
        }
    }
    if (!io.FontDefault)
    {
        fontConfig.SizePixels = fontSize;
        io.FontDefault = io.Fonts->AddFontDefault(&fontConfig);
    }

    const std::filesystem::path relativeMonoPath = "assets/fonts/FiraCode/FiraCode-Regular.ttf";
    const std::filesystem::path monoPaths[] = {
        relativeMonoPath, std::filesystem::path(PROJECT_SOURCE_DIR) / relativeMonoPath};
    std::snprintf(fontConfig.Name, sizeof(fontConfig.Name), "Rubia Console");
    for (const auto& path : monoPaths)
        if (io.Fonts->AddFontFromFileTTF(path.u8string().c_str(), 14.0f, &fontConfig)) break;
}

} // namespace

ImGuiLayer::ImGuiLayer(CreateInfo createInfo)
{
    create(std::move(createInfo));
}

ImGuiLayer::~ImGuiLayer()
{
    reset();
}

void ImGuiLayer::create(CreateInfo createInfo)
{
    if (!createInfo.platform || !createInfo.renderer)
        throw std::invalid_argument("ImGuiLayer requires platform and renderer backends");

    reset();
    platformBackend_ = std::move(createInfo.platform);
    rendererBackend_ = std::move(createInfo.renderer);
    iniFilename_ = createInfo.iniFilename;

    try
    {
        IMGUI_CHECKVERSION();
        imguiContext_ = ImGui::CreateContext();
        makeContextCurrent();

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        if (createInfo.enableDocking)
        {
            io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        }
        io.IniFilename = iniFilename_.empty()
            ? nullptr
            : iniFilename_.c_str();
        applyEditorTheme(themeSettings_);
        loadEditorFont(io);

        platformBackend_->initialize(imguiContext_);
        platformBackendInitialized_ = true;
        const float contentScale = platformBackend_->contentScale();
        if (contentScale > 0.0f && contentScale != 1.0f)
        {
            ImGui::GetStyle().ScaleAllSizes(contentScale);
            ImGui::GetStyle().FontScaleDpi = contentScale;
        }

        rendererBackend_->initialize(imguiContext_);
    }
    catch (...)
    {
        reset();
        throw;
    }
}

void ImGuiLayer::reset() noexcept
{
    makeContextCurrent();
    if (rendererBackend_) rendererBackend_->shutdown();
    rendererBackend_.reset();

    if (platformBackendInitialized_)
    {
        platformBackend_->shutdown();
        platformBackendInitialized_ = false;
    }
    platformBackend_.reset();
    if (imguiContext_ != nullptr)
    {
        ImGui::DestroyContext(imguiContext_);
        imguiContext_ = nullptr;
    }

    iniFilename_.clear();
}

void ImGuiLayer::beginFrame()
{
    if (!*this)
    {
        throw std::logic_error("cannot begin an uninitialized ImGui frame");
    }

    makeContextCurrent();
    rendererBackend_->newFrame();
    platformBackend_->newFrame();
    ImGui::NewFrame();
}

const render::GuiOverlay* ImGuiLayer::endFrame()
{
    if (!*this)
    {
        throw std::logic_error("cannot end an uninitialized ImGui frame");
    }

    makeContextCurrent();
    ImGui::Render();
    rendererBackend_->prepare(ImGui::GetDrawData());
    return rendererBackend_.get();
}

void ImGuiLayer::reconfigureRenderer()
{
    if (!*this) throw std::logic_error("cannot reconfigure an uninitialized GUI layer");
    makeContextCurrent();
    rendererBackend_->reconfigure();
}

integration::imgui::GuiRendererBackend& ImGuiLayer::rendererBackend()
{
    if (!*this) throw std::logic_error("GUI renderer backend is unavailable");
    return *rendererBackend_;
}

ImGuiLayer::operator bool() const noexcept
{
    return imguiContext_ && platformBackendInitialized_ && rendererBackend_ && rendererBackend_->ready();
}

void ImGuiLayer::makeContextCurrent() const noexcept
{
    if (imguiContext_ != nullptr)
    {
        ImGui::SetCurrentContext(imguiContext_);
    }
}

} // namespace rubia::editor
