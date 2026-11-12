#pragma once
#include "imgui/GuiBackend.hpp"
struct GLFWwindow;
namespace rubia::integration::imgui
{
// Select the official GLFW initialization path without leaking it into the UI layer.
enum class GlfwGuiClientApi { Vulkan, OpenGL, Other };
std::unique_ptr<GuiPlatformBackend> makeGlfwGuiPlatform(GLFWwindow*, GlfwGuiClientApi);
}
