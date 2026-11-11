add_library(rubia_vulkan_glfw STATIC Surface.cpp Surface.hpp)
add_library(Rubia::VulkanGlfw ALIAS rubia_vulkan_glfw)

target_include_directories(rubia_vulkan_glfw PUBLIC
    "${PROJECT_SOURCE_DIR}/integrations"
)
target_link_libraries(rubia_vulkan_glfw
    PUBLIC Vulkan::Vulkan rubia_project_options
    PRIVATE Rubia::PlatformGlfw glfw
)
