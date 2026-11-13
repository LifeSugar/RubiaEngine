#pragma once

#include "glfw/Window.hpp"

#include <vulkan/vulkan.h>

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>

#include <cstdint>
#include <string>
#include <vector>

namespace rubia::rhi::vulkan
{

/// Compatibility facade. New platform callers use platform::glfw::Window.
class Window final
{
public:
    /// Parameters used to create a GLFW window.
    struct CreateInfo
    {
        /// Initial framebuffer width in pixels.
        uint32_t width = 1280;
        /// Initial framebuffer height in pixels.
        uint32_t height = 720;
        /// Text displayed in the window title bar.
        std::string title = "Vulkan";
        /// Whether the native window is shown after creation.
        bool visible = true;
    };

    /// Creates an empty window wrapper.
    Window() = default;
    /// Creates a window from the supplied settings.
    explicit Window(const CreateInfo& createInfo);
    /// Destroys the window and releases its GLFW runtime reference.
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    /// Window ownership cannot be moved because GLFW stores this object's address.
    Window(Window&&) = delete;
    /// Window ownership cannot be move-assigned.
    Window& operator=(Window&&) = delete;

    /// Creates or replaces the owned window.
    void create(const CreateInfo& createInfo);
    /// Destroys the owned window and clears its state.
    void reset() noexcept;

    /// Processes all pending window events without blocking.
    void pollEvents() const;
    /// Blocks until at least one window event arrives.
    void waitEvents() const;
    /// Waits for window events up to the specified duration.
    void waitEventsTimeout(double timeoutSeconds) const;

    /// Returns whether the window has been asked to close.
    [[nodiscard]] bool shouldClose() const;
    /// Returns the current drawable framebuffer size in pixels.
    [[nodiscard]] VkExtent2D framebufferExtent() const;
    /// Returns and clears the framebuffer-resized flag.
    [[nodiscard]] bool consumeFramebufferResize() noexcept;

    /// Creates a Vulkan surface for the owned window.
    [[nodiscard]] VkSurfaceKHR createVulkanSurface(VkInstance instance) const;
    /// Returns the Vulkan instance extensions required by GLFW.
    [[nodiscard]] static std::vector<const char*> requiredVulkanInstanceExtensions();
    /// Returns GLFW's monotonic time in seconds.
    [[nodiscard]] static double time() noexcept;

    /// Returns the non-owning native handle used by platform integrations.
    [[nodiscard]] GLFWwindow* nativeHandle() const noexcept { return window_.nativeHandle(); }
    [[nodiscard]] const platform::glfw::Window& platformWindow() const noexcept
    {
        return window_;
    }

    /// Returns whether a window is currently owned.
    [[nodiscard]] explicit operator bool() const noexcept;

private:
    platform::glfw::Window window_;
};

} // namespace rubia::rhi::vulkan
