#include "glfw/Window.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <mutex>
#include <stdexcept>

namespace rubia::platform::glfw
{
namespace
{

std::mutex gGlfwRuntimeMutex;
uint32_t gGlfwRuntimeUsers = 0;

void acquireGlfwRuntime()
{
    std::lock_guard<std::mutex> lock(gGlfwRuntimeMutex);
    if (gGlfwRuntimeUsers == 0 && glfwInit() != GLFW_TRUE)
    {
        throw std::runtime_error("failed to initialize GLFW");
    }
    ++gGlfwRuntimeUsers;
}

void releaseGlfwRuntime() noexcept
{
    std::lock_guard<std::mutex> lock(gGlfwRuntimeMutex);
    if (gGlfwRuntimeUsers == 0)
    {
        return;
    }

    --gGlfwRuntimeUsers;
    if (gGlfwRuntimeUsers == 0)
    {
        glfwTerminate();
    }
}

} // namespace

Window::Window(const CreateInfo& createInfo)
{
    create(createInfo);
}

Window::~Window()
{
    reset();
}

void Window::create(const CreateInfo& createInfo)
{
    if (createInfo.width == 0 || createInfo.height == 0)
    {
        throw std::invalid_argument("window dimensions must be greater than zero");
    }

    acquireGlfwRuntime();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(
        GLFW_VISIBLE,
        createInfo.visible ? GLFW_TRUE : GLFW_FALSE);
    GLFWwindow* newHandle = glfwCreateWindow(
        static_cast<int>(createInfo.width),
        static_cast<int>(createInfo.height),
        createInfo.title.c_str(),
        nullptr,
        nullptr);
    if (newHandle == nullptr)
    {
        releaseGlfwRuntime();
        throw std::runtime_error("failed to create GLFW window");
    }

    // Acquire the replacement before releasing the old window so replacing the
    // only window cannot terminate GLFW between the two operations.
    reset();
    handle_ = newHandle;
    ownsGlfwRuntime_ = true;
    framebufferResized_ = false;
    glfwSetWindowUserPointer(handle_, this);
    glfwSetFramebufferSizeCallback(handle_, framebufferResizeCallback);
}

void Window::reset() noexcept
{
    if (handle_ != nullptr)
    {
        glfwDestroyWindow(handle_);
        handle_ = nullptr;
    }

    if (ownsGlfwRuntime_)
    {
        releaseGlfwRuntime();
        ownsGlfwRuntime_ = false;
    }
    framebufferResized_ = false;
}

void Window::pollEvents() const
{
    glfwPollEvents();
}

void Window::waitEvents() const
{
    glfwWaitEvents();
}

void Window::waitEventsTimeout(double timeoutSeconds) const
{
    glfwWaitEventsTimeout(timeoutSeconds);
}

bool Window::shouldClose() const
{
    if (handle_ == nullptr)
    {
        return true;
    }
    return glfwWindowShouldClose(handle_) == GLFW_TRUE;
}

Extent2D Window::framebufferExtent() const
{
    if (handle_ == nullptr)
    {
        return {};
    }

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(handle_, &width, &height);
    return {
        static_cast<uint32_t>(std::max(width, 0)),
        static_cast<uint32_t>(std::max(height, 0))
    };
}

bool Window::consumeFramebufferResize() noexcept
{
    const bool resized = framebufferResized_;
    framebufferResized_ = false;
    return resized;
}

double Window::time() noexcept
{
    return glfwGetTime();
}

Window::operator bool() const noexcept
{
    return handle_ != nullptr;
}

void Window::framebufferResizeCallback(GLFWwindow* window, int, int)
{
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self != nullptr)
    {
        self->framebufferResized_ = true;
    }
}

} // namespace rubia::platform::glfw
