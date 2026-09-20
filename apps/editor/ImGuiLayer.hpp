#pragma once

#include "EditorFwd.hpp"
#include "EditorTheme.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>

struct ImDrawData;
struct ImGuiContext;

namespace rubia::editor
{

/// Owns the Dear ImGui context and its GLFW/Vulkan backends.
class ImGuiLayer final
{
public:
    struct CreateInfo
    {
        rhi::vulkan::Window* window = nullptr;
        rhi::vulkan::VulkanContext* context = nullptr;
        VkRenderPass renderPass = VK_NULL_HANDLE;
        uint32_t minImageCount = 2;
        uint32_t imageCount = 0;
        bool enableDocking = true;
        std::string iniFilename;
    };

    ImGuiLayer() = default;
    explicit ImGuiLayer(const CreateInfo& createInfo);
    ~ImGuiLayer();

    ImGuiLayer(const ImGuiLayer&) = delete;
    ImGuiLayer& operator=(const ImGuiLayer&) = delete;
    ImGuiLayer(ImGuiLayer&&) = delete;
    ImGuiLayer& operator=(ImGuiLayer&&) = delete;

    void create(const CreateInfo& createInfo);
    void reset() noexcept;

    /// Starts one Dear ImGui frame after window events have been processed.
    void beginFrame();
    /// Finalizes the UI and returns draw data valid until the next frame.
    [[nodiscard]] ImDrawData* endFrame();

    /// Releases objects tied to the current swapchain render pass.
    void shutdownRendererBackend() noexcept;
    /// Recreates the Vulkan backend for a replacement swapchain render pass.
    void initializeRendererBackend(
        VkRenderPass renderPass,
        uint32_t imageCount);
    /// Replaces only the pipeline tied to the swapchain render pass.
    void recreateRendererPipeline(VkRenderPass renderPass);

    [[nodiscard]] explicit operator bool() const noexcept;

private:
    void makeContextCurrent() const noexcept;

    rhi::vulkan::VulkanContext* context_ = nullptr;
    ImGuiContext* imguiContext_ = nullptr;
    EditorThemeSettings themeSettings_;
    std::string iniFilename_;
    uint32_t minImageCount_ = 2;
    bool platformBackendInitialized_ = false;
    bool rendererBackendInitialized_ = false;
};

} // namespace rubia::editor
