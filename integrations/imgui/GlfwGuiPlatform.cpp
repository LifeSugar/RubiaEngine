#include "imgui/GlfwGuiPlatform.hpp"
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <stdexcept>

namespace rubia::integration::imgui
{
namespace
{
class GlfwGuiPlatform final : public GuiPlatformBackend
{
public:
    GlfwGuiPlatform(GLFWwindow* window, GlfwGuiClientApi api) : window_(window), api_(api)
    {
        if (!window_) throw std::invalid_argument("GUI platform requires a window");
    }
    ~GlfwGuiPlatform() override { shutdown(); }
    void initialize(ImGuiContext* context) override
    {
        if (!context || context_) throw std::logic_error("invalid GLFW GUI initialization");
        ImGui::SetCurrentContext(context);
        bool initialized = false;
        switch (api_)
        {
        case GlfwGuiClientApi::Vulkan: initialized = ImGui_ImplGlfw_InitForVulkan(window_, true); break;
        case GlfwGuiClientApi::OpenGL: initialized = ImGui_ImplGlfw_InitForOpenGL(window_, true); break;
        case GlfwGuiClientApi::Other: initialized = ImGui_ImplGlfw_InitForOther(window_, true); break;
        }
        if (!initialized) throw std::runtime_error("failed to initialize ImGui GLFW platform");
        context_ = context;
    }
    void shutdown() noexcept override
    {
        if (context_)
        {
            ImGui::SetCurrentContext(context_);
            ImGui_ImplGlfw_Shutdown();
            context_ = nullptr;
        }
    }
    void newFrame() override
    {
        if (!context_) throw std::logic_error("GUI platform is not initialized");
        ImGui::SetCurrentContext(context_);
        ImGui_ImplGlfw_NewFrame();
    }
    float contentScale() const override { return ImGui_ImplGlfw_GetContentScaleForWindow(window_); }
private:
    GLFWwindow* window_;
    GlfwGuiClientApi api_;
    ImGuiContext* context_ = nullptr;
};
}
std::unique_ptr<GuiPlatformBackend> makeGlfwGuiPlatform(GLFWwindow* window, GlfwGuiClientApi api)
{
    return std::make_unique<GlfwGuiPlatform>(window, api);
}
}
