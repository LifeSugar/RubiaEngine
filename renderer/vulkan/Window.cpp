#include "vulkan/Window.hpp"

#include "vulkan_glfw/Surface.hpp"

namespace rubia::rhi::vulkan
{

Window::Window(const CreateInfo& createInfo)
{
    create(createInfo);
}

Window::~Window() = default;

void Window::create(const CreateInfo& createInfo)
{
    window_.create({createInfo.width, createInfo.height, createInfo.title, createInfo.visible});
}

void Window::reset() noexcept
{
    window_.reset();
}

void Window::pollEvents() const
{
    window_.pollEvents();
}

void Window::waitEvents() const
{
    window_.waitEvents();
}

void Window::waitEventsTimeout(double timeoutSeconds) const
{
    window_.waitEventsTimeout(timeoutSeconds);
}

bool Window::shouldClose() const
{
    return window_.shouldClose();
}

VkExtent2D Window::framebufferExtent() const
{
    const auto extent = window_.framebufferExtent();
    return {extent.width, extent.height};
}

bool Window::consumeFramebufferResize() noexcept
{
    return window_.consumeFramebufferResize();
}

VkSurfaceKHR Window::createVulkanSurface(VkInstance instance) const
{
    return integration::vulkan_glfw::createSurface(window_, instance);
}

std::vector<const char*> Window::requiredVulkanInstanceExtensions()
{
    return integration::vulkan_glfw::requiredInstanceExtensions();
}

double Window::time() noexcept
{
    return platform::glfw::Window::time();
}

Window::operator bool() const noexcept
{
    return static_cast<bool>(window_);
}

} // namespace rubia::rhi::vulkan
