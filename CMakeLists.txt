cmake_minimum_required(VERSION 3.23)

project(RubiaEngine VERSION 0.1.0 LANGUAGES CXX)

option(VK_RENDERER_USE_VMA "Use Vulkan Memory Allocator in renderer resources" ON)
option(RUBIA_BUILD_VULKAN "Build the Vulkan backend and its platform integration" ON)
option(RUBIA_BUILD_IMPORTERS "Build asset importers and shader tools" ON)
option(RUBIA_BUILD_APPS "Build the editor/runtime application" ON)
if(RUBIA_BUILD_APPS AND (NOT RUBIA_BUILD_VULKAN OR NOT RUBIA_BUILD_IMPORTERS))
    message(FATAL_ERROR "The current application requires RUBIA_BUILD_VULKAN and RUBIA_BUILD_IMPORTERS")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Dependencies)
include(CTest)

add_library(rubia_project_options INTERFACE)
target_compile_features(rubia_project_options INTERFACE cxx_std_17)
target_compile_options(rubia_project_options INTERFACE
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
)

add_subdirectory(engine)
add_subdirectory(rhi)
if(RUBIA_BUILD_IMPORTERS)
    add_subdirectory(importers)
endif()
if(RUBIA_BUILD_VULKAN)
    add_subdirectory(platform/glfw)
    add_subdirectory(integrations/vulkan_glfw)
endif()
add_subdirectory(renderer)
add_subdirectory(integrations/imgui)
if(RUBIA_BUILD_APPS)
    add_subdirectory(apps)
endif()
add_subdirectory(tests)

include(CheckRenderBoundaries)
rubia_check_render_boundaries()
