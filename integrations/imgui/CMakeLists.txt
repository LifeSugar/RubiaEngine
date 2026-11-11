add_library(rubia_imgui_vulkan_adapter STATIC
    VulkanGuiRenderer.cpp
    VulkanGuiRenderer.hpp
    VulkanApplicationGuiRenderBridge.cpp
    VulkanApplicationGuiRenderBridge.hpp
)
add_library(Rubia::ImGuiVulkanAdapter ALIAS rubia_imgui_vulkan_adapter)

target_include_directories(rubia_imgui_vulkan_adapter
    PUBLIC "${PROJECT_SOURCE_DIR}/integrations"
    PRIVATE "${PROJECT_SOURCE_DIR}/renderer"
)
target_link_libraries(rubia_imgui_vulkan_adapter
    PUBLIC Rubia::Engine Rubia::ImGuiVulkan Rubia::RendererVulkan rubia_project_options
)
