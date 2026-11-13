#include "vulkan_glfw/Surface.hpp"

#include "glfw/Window.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <stdexcept>

namespace rubia::integration::vulkan_glfw
{

std::vector<const char*> requiredInstanceExtensions()
{
    uint32_t extensionCount = 0;
    const char** extensions = glfwGetRequiredInstanceExtensions(&extensionCount);
    if (extensions == nullptr || extensionCount == 0)
    {
        throw std::runtime_error("GLFW did not provide Vulkan instance extensions");
    }
    return {extensions, extensions + extensionCount};
}

VkSurfaceKHR createSurface(const platform::glfw::Window& window, VkInstance instance)
{
    if (!window)
    {
        throw std::logic_error("cannot create a Vulkan surface without a window");
    }

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (glfwCreateWindowSurface(instance, window.nativeHandle(), nullptr, &surface) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to create window surface");
    }
    return surface;
}

} // namespace rubia::integration::vulkan_glfw
