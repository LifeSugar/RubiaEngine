find_package(glm CONFIG REQUIRED)

if(RUBIA_BUILD_VULKAN)
    find_package(Vulkan REQUIRED)

    add_library(VulkanMemoryAllocator INTERFACE)
    target_include_directories(VulkanMemoryAllocator INTERFACE
        "${PROJECT_SOURCE_DIR}/third_party/vma/include"
    )
    target_link_libraries(VulkanMemoryAllocator INTERFACE Vulkan::Vulkan)
endif()

# The compatibility Vulkan renderer still owns presentation and GUI integration.
# Keep GLFW optional for CPU-only builds until those callers finish migrating.
if(RUBIA_BUILD_VULKAN OR RUBIA_BUILD_APPS)
    find_package(glfw3 CONFIG REQUIRED)
endif()

if(RUBIA_BUILD_IMPORTERS)
    # Host-side shader compiler only; no Slang library leaks into Engine or RHI.
    find_package(SlangCompiler 2026.1 REQUIRED)
    get_filename_component(_slang_bin "${SlangCompiler_EXECUTABLE}" DIRECTORY)
    get_filename_component(_slang_root "${_slang_bin}" DIRECTORY)
    find_path(Slang_INCLUDE_DIR slang.h HINTS "${_slang_root}" PATH_SUFFIXES include/slang include Include/slang NO_DEFAULT_PATH REQUIRED)
    find_library(Slang_LIBRARY NAMES slang-compiler slang HINTS "${_slang_root}" PATH_SUFFIXES lib Lib NO_DEFAULT_PATH REQUIRED)
    add_library(Slang::Compiler SHARED IMPORTED GLOBAL)
    set_target_properties(Slang::Compiler PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${Slang_INCLUDE_DIR}")
    if(WIN32)
        find_file(Slang_RUNTIME NAMES slang-compiler.dll HINTS "${_slang_bin}" NO_DEFAULT_PATH REQUIRED)
        set_target_properties(Slang::Compiler PROPERTIES IMPORTED_IMPLIB "${Slang_LIBRARY}" IMPORTED_LOCATION "${Slang_RUNTIME}")
    else()
        set_target_properties(Slang::Compiler PROPERTIES IMPORTED_LOCATION "${Slang_LIBRARY}")
    endif()

    find_package(assimp CONFIG REQUIRED)
    find_package(Stb REQUIRED)
    find_package(Ktx CONFIG REQUIRED)

    # Prefer a standalone reflection dependency; importers do not need a Vulkan
    # loader/device. Retain the SDK layout fallback for existing installations.
    find_package(spirv_cross_core CONFIG QUIET)
    if(NOT TARGET spirv-cross-core)
        set(rubia_spirv_sdk_roots)
        if(DEFINED ENV{VULKAN_SDK})
            file(TO_CMAKE_PATH "$ENV{VULKAN_SDK}" rubia_spirv_sdk_root)
            list(APPEND rubia_spirv_sdk_roots "${rubia_spirv_sdk_root}")
        endif()
        if(Vulkan_INCLUDE_DIRS)
            list(GET Vulkan_INCLUDE_DIRS 0 rubia_vulkan_include_root)
            get_filename_component(rubia_spirv_sdk_root
                "${rubia_vulkan_include_root}" DIRECTORY)
            list(APPEND rubia_spirv_sdk_roots "${rubia_spirv_sdk_root}")
        endif()

        find_path(SPIRV_CROSS_INCLUDE_DIR spirv_cross.hpp
            HINTS ${rubia_spirv_sdk_roots}
            PATH_SUFFIXES Include/spirv_cross include/spirv_cross spirv_cross
        )
        find_library(SPIRV_CROSS_CORE_RELEASE spirv-cross-core
            HINTS ${rubia_spirv_sdk_roots}
            PATH_SUFFIXES Lib lib
        )
        if(MSVC)
            find_library(SPIRV_CROSS_CORE_DEBUG spirv-cross-cored
                HINTS ${rubia_spirv_sdk_roots}
                PATH_SUFFIXES Lib lib
            )
        endif()

        if(NOT SPIRV_CROSS_INCLUDE_DIR OR NOT SPIRV_CROSS_CORE_RELEASE)
            message(FATAL_ERROR
                "Importers require SPIRV-Cross core; install its CMake package or set "
                "SPIRV_CROSS_INCLUDE_DIR and SPIRV_CROSS_CORE_RELEASE")
        endif()

        add_library(spirv-cross-core STATIC IMPORTED)
        set_target_properties(spirv-cross-core PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${SPIRV_CROSS_INCLUDE_DIR}"
            IMPORTED_LOCATION "${SPIRV_CROSS_CORE_RELEASE}"
        )
        if(MSVC AND SPIRV_CROSS_CORE_DEBUG)
            set_target_properties(spirv-cross-core PROPERTIES
                IMPORTED_LOCATION_DEBUG "${SPIRV_CROSS_CORE_DEBUG}"
            )
            set_property(TARGET spirv-cross-core APPEND PROPERTY
                INTERFACE_LINK_OPTIONS "$<$<CONFIG:Debug>:/IGNORE:4099>"
            )
        endif()
    endif()
endif()

set(IMGUI_DIR "${PROJECT_SOURCE_DIR}/third_party/imgui")
add_library(rubia_imgui_core STATIC EXCLUDE_FROM_ALL
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
)
add_library(Rubia::ImGuiCore ALIAS rubia_imgui_core)
target_include_directories(rubia_imgui_core PUBLIC "${IMGUI_DIR}")
target_compile_options(rubia_imgui_core PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
)

# Preserve the old link name while callers select narrower targets over time.
add_library(imgui INTERFACE)
target_link_libraries(imgui INTERFACE Rubia::ImGuiCore)

if(RUBIA_BUILD_VULKAN OR RUBIA_BUILD_APPS)
    add_library(rubia_imgui_glfw STATIC
        "${IMGUI_DIR}/backends/imgui_impl_glfw.cpp"
    )
    add_library(Rubia::ImGuiGlfw ALIAS rubia_imgui_glfw)
    target_include_directories(rubia_imgui_glfw PUBLIC "${IMGUI_DIR}/backends")
    target_compile_definitions(rubia_imgui_glfw PRIVATE GLFW_INCLUDE_NONE)
    target_link_libraries(rubia_imgui_glfw PUBLIC Rubia::ImGuiCore PRIVATE glfw)
    target_link_libraries(imgui INTERFACE Rubia::ImGuiGlfw)
endif()

if(RUBIA_BUILD_VULKAN)
    add_library(rubia_imgui_vulkan STATIC
        "${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp"
    )
    add_library(Rubia::ImGuiVulkan ALIAS rubia_imgui_vulkan)
    target_include_directories(rubia_imgui_vulkan PUBLIC "${IMGUI_DIR}/backends")
    # The backend's public API contains Vk types. Only this adapter exposes the SDK.
    target_link_libraries(rubia_imgui_vulkan
        PUBLIC Rubia::ImGuiCore Vulkan::Vulkan
    )
    target_link_libraries(imgui INTERFACE Rubia::ImGuiVulkan)
endif()
