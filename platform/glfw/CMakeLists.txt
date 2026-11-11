add_library(rubia_platform_glfw STATIC Window.cpp Window.hpp ../Extent2D.hpp)
add_library(Rubia::PlatformGlfw ALIAS rubia_platform_glfw)

target_include_directories(rubia_platform_glfw PUBLIC
    "${PROJECT_SOURCE_DIR}/platform"
)
target_link_libraries(rubia_platform_glfw
    PUBLIC rubia_project_options
    PRIVATE glfw
)
