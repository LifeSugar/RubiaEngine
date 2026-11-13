#pragma once

#include <vulkan/vulkan.h>

#include <vector>

namespace rubia::platform::glfw
{
class Window;
}

namespace rubia::integration::vulkan_glfw
{

/// GLFW must be initialized. Names remain borrowed from GLFW until termination.
[[nodiscard]] std::vector<const char*> requiredInstanceExtensions();

/// Returns an owned surface. The caller destroys it before its instance/window.
[[nodiscard]] VkSurfaceKHR createSurface(
    const platform::glfw::Window& window,
    VkInstance instance);

} // namespace rubia::integration::vulkan_glfw
