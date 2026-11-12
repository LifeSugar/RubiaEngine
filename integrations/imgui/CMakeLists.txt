# GUI contracts and resource registration bridge have no native API dependency.
add_library(rubia_gui_integration STATIC
    GuiBackend.hpp
    ApplicationGuiRenderBridge.cpp
    ApplicationGuiRenderBridge.hpp
)
add_library(Rubia::GuiIntegration ALIAS rubia_gui_integration)
target_include_directories(rubia_gui_integration PUBLIC "${PROJECT_SOURCE_DIR}/integrations")
target_link_libraries(rubia_gui_integration PUBLIC Rubia::Renderer)

if(TARGET Rubia::ImGuiGlfw)
    add_library(rubia_imgui_glfw_adapter STATIC GlfwGuiPlatform.cpp GlfwGuiPlatform.hpp)
    add_library(Rubia::ImGuiGlfwAdapter ALIAS rubia_imgui_glfw_adapter)
    target_link_libraries(rubia_imgui_glfw_adapter PUBLIC Rubia::GuiIntegration PRIVATE Rubia::ImGuiGlfw)
endif()

if(RUBIA_BUILD_VULKAN)
    add_library(rubia_imgui_vulkan_adapter STATIC
        VulkanGuiRenderer.cpp
        VulkanGuiRenderer.hpp
        VulkanGuiRenderHost.hpp
    )
    add_library(Rubia::ImGuiVulkanAdapter ALIAS rubia_imgui_vulkan_adapter)
    target_link_libraries(rubia_imgui_vulkan_adapter
        PUBLIC Rubia::GuiIntegration Rubia::RendererVulkan
        PRIVATE Rubia::ImGuiVulkan
    )
endif()
