#pragma once

#include "Extent2D.hpp"

#include <cstdint>
#include <string>

struct GLFWwindow;

namespace rubia::platform::glfw
{

/// Owns a GLFW window and its share of the GLFW runtime.
class Window final
{
public:
    struct CreateInfo
    {
        uint32_t width = 1280;
        uint32_t height = 720;
        std::string title = "RubiaEngine";
        bool visible = true;
    };

    Window() = default;
    explicit Window(const CreateInfo& createInfo);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    // GLFW stores this object's address for framebuffer notifications.
    Window(Window&&) = delete;
    Window& operator=(Window&&) = delete;

    void create(const CreateInfo& createInfo);
    void reset() noexcept;

    void pollEvents() const;
    void waitEvents() const;
    void waitEventsTimeout(double timeoutSeconds) const;

    [[nodiscard]] bool shouldClose() const;
    [[nodiscard]] Extent2D framebufferExtent() const;
    [[nodiscard]] bool consumeFramebufferResize() noexcept;
    [[nodiscard]] static double time() noexcept;

    /// Borrowed handle for platform integrations; invalid after reset/create.
    [[nodiscard]] GLFWwindow* nativeHandle() const noexcept { return handle_; }
    [[nodiscard]] explicit operator bool() const noexcept;

private:
    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);

    GLFWwindow* handle_ = nullptr;
    bool ownsGlfwRuntime_ = false;
    bool framebufferResized_ = false;
};

} // namespace rubia::platform::glfw
