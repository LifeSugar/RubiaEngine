# These checks are intentionally independent of Vulkan discovery. Run them at
# configure time and through CTest so a later include cannot silently erase a
# boundary which was established by splitting the CMake targets.
function(rubia_assert_target_links target)
    if(NOT TARGET "${target}")
        return()
    endif()
    foreach(property LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
        get_target_property(links "${target}" "${property}")
        if(NOT links)
            continue()
        endif()
        foreach(link IN LISTS links)
            string(REGEX REPLACE "^\\$<LINK_ONLY:([^>]+)>$" "\\1" link "${link}")
            if(NOT link IN_LIST ARGN)
                message(FATAL_ERROR "Render boundary: ${target} may not link ${link} (${property})")
            endif()
        endforeach()
    endforeach()
endfunction()

function(rubia_assert_source_includes label forbidden)
    foreach(source IN LISTS ARGN)
        if(NOT EXISTS "${source}")
            message(FATAL_ERROR "Render boundary: missing ${label} source ${source}")
        endif()
        file(STRINGS "${source}" includes REGEX "^[ \t]*#[ \t]*include[ \t]*[<\"]")
        foreach(line IN LISTS includes)
            if(line MATCHES "[<\"](${forbidden})")
                message(FATAL_ERROR "Render boundary: ${label}: ${source}: ${line}")
            endif()
        endforeach()
    endforeach()
endfunction()

function(rubia_scan_render_boundaries)
    file(GLOB_RECURSE engine_sources "${RUBIA_BOUNDARY_ROOT}/engine/*.hpp" "${RUBIA_BOUNDARY_ROOT}/engine/*.cpp")
    file(GLOB_RECURSE rhi_headers "${RUBIA_BOUNDARY_ROOT}/rhi/include/*.hpp")
    set(renderer_sources)
    foreach(directory core pipeline resources passes)
        file(GLOB_RECURSE sources "${RUBIA_BOUNDARY_ROOT}/renderer/${directory}/*.hpp" "${RUBIA_BOUNDARY_ROOT}/renderer/${directory}/*.cpp")
        list(APPEND renderer_sources ${sources})
    endforeach()
    set(native_headers "(vulkan/|GLFW/|imgui|backends/imgui|vk_mem_alloc)")
    rubia_assert_source_includes(Engine "${native_headers}|rhi/|core/|pipeline/|resources/|passes/" ${engine_sources})
    rubia_assert_source_includes(Rhi "${native_headers}|glm/|asset/|render/|scene/|math/|core/|pipeline/|resources/|passes/" ${rhi_headers})
    rubia_assert_source_includes(Renderer "${native_headers}" ${renderer_sources})
    rubia_assert_source_includes(RhiVulkan "GLFW/|imgui|backends/imgui|glm/|asset/|render/|scene/|math/|core/|pipeline/|resources/|passes/" ${RUBIA_BOUNDARY_NATIVE_SOURCES})
    # EditorCore deliberately depends on ImGui core; only its native backends
    # and the application host are forbidden here.
    rubia_assert_source_includes(EditorCore "vulkan/|GLFW/|imgui_impl_|backends/imgui|vk_mem_alloc|App\\.hpp|ImGuiLayer\\.hpp" ${RUBIA_BOUNDARY_EDITOR_SOURCES})
    rubia_assert_source_includes(PlatformWindow "vulkan/|GLFW/" "${RUBIA_BOUNDARY_ROOT}/platform/glfw/Window.hpp")
    set(application_runtime_sources
        "${RUBIA_BOUNDARY_ROOT}/apps/editor/App.hpp"
        "${RUBIA_BOUNDARY_ROOT}/apps/editor/backend/ApplicationBackend.hpp"
        "${RUBIA_BOUNDARY_ROOT}/apps/editor/AppFrameLoop.cpp"
        "${RUBIA_BOUNDARY_ROOT}/apps/editor/AppResize.cpp"
        "${RUBIA_BOUNDARY_ROOT}/apps/editor/AppContentLoading.cpp"
        "${RUBIA_BOUNDARY_ROOT}/apps/editor/AppTextureReimport.cpp")
    rubia_assert_source_includes(AppRuntime "vulkan/|GLFW/|imgui_impl_|imgui/Vulkan|backend/Vulkan" ${application_runtime_sources})
    foreach(source IN LISTS application_runtime_sources)
        file(READ "${source}" source_text)
        if(source_text MATCHES "Vk[A-Z][A-Za-z0-9_]*|rhi::vulkan::|VulkanApplicationBackend")
            message(FATAL_ERROR "Render boundary: native Vulkan type leaked into application runtime: ${source}")
        endif()
    endforeach()
    rubia_assert_source_includes(GuiIntegration "vulkan/|GLFW/|imgui_impl_|backends/imgui|imgui/Vulkan|imgui/Glfw"
        "${RUBIA_BOUNDARY_ROOT}/integrations/imgui/GuiBackend.hpp"
        "${RUBIA_BOUNDARY_ROOT}/integrations/imgui/ApplicationGuiRenderBridge.hpp"
        "${RUBIA_BOUNDARY_ROOT}/integrations/imgui/ApplicationGuiRenderBridge.cpp"
        "${RUBIA_BOUNDARY_ROOT}/apps/editor/ImGuiLayer.hpp"
        "${RUBIA_BOUNDARY_ROOT}/apps/editor/ImGuiLayer.cpp")
endfunction()

function(rubia_absolute_target_sources target output)
    set(result)
    if(TARGET "${target}")
        get_target_property(directory "${target}" SOURCE_DIR)
        get_target_property(sources "${target}" SOURCES)
        foreach(source IN LISTS sources)
            if(NOT IS_ABSOLUTE "${source}")
                set(source "${directory}/${source}")
            endif()
            list(APPEND result "${source}")
        endforeach()
    endif()
    set(${output} "${result}" PARENT_SCOPE)
endfunction()

function(rubia_check_render_boundaries)
    rubia_assert_target_links(rubia_engine glm::glm rubia_project_options)
    rubia_assert_target_links(rubia_rhi rubia_project_options)
    rubia_assert_target_links(rubia_renderer Rubia::Engine Rubia::Rhi)
    rubia_assert_target_links(rubia_gui_integration Rubia::Renderer)
    rubia_assert_target_links(rubia_editor_gui Rubia::EditorCore Rubia::GuiIntegration Rubia::ImGuiCore)
    rubia_assert_target_links(rubia_rhi_vulkan Rubia::Rhi Vulkan::Vulkan VulkanMemoryAllocator rubia_project_options)
    rubia_assert_target_links(rubia_editor_core Rubia::Engine Rubia::Importers Rubia::ImGuiCore rubia_project_options)
    set(RUBIA_BOUNDARY_ROOT "${PROJECT_SOURCE_DIR}")
    rubia_absolute_target_sources(rubia_rhi_vulkan RUBIA_BOUNDARY_NATIVE_SOURCES)
    rubia_absolute_target_sources(rubia_editor_core RUBIA_BOUNDARY_EDITOR_SOURCES)
    rubia_scan_render_boundaries()
    set(manifest "${PROJECT_BINARY_DIR}/render-boundary-sources.cmake")
    file(WRITE "${manifest}"
        "set(RUBIA_BOUNDARY_ROOT [==[${RUBIA_BOUNDARY_ROOT}]==])\n"
        "set(RUBIA_BOUNDARY_NATIVE_SOURCES [==[${RUBIA_BOUNDARY_NATIVE_SOURCES}]==])\n"
        "set(RUBIA_BOUNDARY_EDITOR_SOURCES [==[${RUBIA_BOUNDARY_EDITOR_SOURCES}]==])\n")
    if(BUILD_TESTING)
        add_test(NAME render-boundary-test COMMAND "${CMAKE_COMMAND}"
            "-DRUBIA_BOUNDARY_MANIFEST=${manifest}" -P "${CMAKE_CURRENT_FUNCTION_LIST_FILE}")
    endif()
endfunction()

if(CMAKE_SCRIPT_MODE_FILE STREQUAL CMAKE_CURRENT_LIST_FILE)
    if(NOT RUBIA_BOUNDARY_MANIFEST)
        message(FATAL_ERROR "Pass -DRUBIA_BOUNDARY_MANIFEST=<build>/render-boundary-sources.cmake")
    endif()
    include("${RUBIA_BOUNDARY_MANIFEST}")
    rubia_scan_render_boundaries()
    message(STATUS "Render source boundaries passed")
endif()
