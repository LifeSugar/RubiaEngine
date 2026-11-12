option(RUBIA_ENABLE_GPU_TESTS "Register hidden-window Vulkan smoke tests" OFF)
if(RUBIA_ENABLE_GPU_TESTS AND NOT RUBIA_BUILD_APPS)
    message(FATAL_ERROR "GPU smoke tests require RUBIA_BUILD_APPS")
endif()

if(RUBIA_BUILD_APPS)
    add_library(rubia_smoke_tests STATIC
        PreparationGallery.cpp
        EditorAssetImportTests.cpp
        AppSmokeTests.cpp
        AppStartupTests.cpp
        AssetPreparationTests.cpp
        VulkanUploadTests.cpp
        VulkanUploadTests.hpp
        AppSmokeTests.hpp
    )
    target_include_directories(rubia_smoke_tests PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_link_libraries(rubia_smoke_tests
        PUBLIC
            Rubia::AppRuntime
            rubia_project_options
        PRIVATE
            Rubia::VulkanApplicationBackend
            imgui
            KTX::ktx
    )

    target_compile_definitions(rubia_smoke_tests PRIVATE PROJECT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

    # The editor keeps its existing command-line smoke-test entry points.
    target_link_libraries(VulkanApp PRIVATE rubia_smoke_tests)
endif()

if(BUILD_TESTING)
    if(RUBIA_BUILD_IMPORTERS)
        add_executable(shader_compiler_tests ShaderCompilerTests.cpp)
        target_link_libraries(shader_compiler_tests PRIVATE Rubia::Importers)
        if(WIN32)
            add_custom_command(TARGET shader_compiler_tests POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Slang::Compiler>" "$<TARGET_FILE_DIR:shader_compiler_tests>")
        endif()
        target_compile_definitions(shader_compiler_tests PRIVATE PROJECT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
        add_test(NAME shader-compiler-test COMMAND shader_compiler_tests)
        set_tests_properties(shader-compiler-test PROPERTIES TIMEOUT 120)
    endif()
    add_executable(asset_revision_tests AssetRevisionTests.cpp)
    target_link_libraries(asset_revision_tests PRIVATE Rubia::Engine rubia_project_options)
    add_test(NAME asset-revision-test COMMAND asset_revision_tests)
    add_executable(shader_program_tests ShaderProgramTests.cpp)
    target_link_libraries(shader_program_tests PRIVATE Rubia::Engine rubia_project_options)
    add_test(NAME shader-program-test COMMAND shader_program_tests)
    add_executable(material_pipeline_tests MaterialPipelineCompilerTests.cpp)
    target_link_libraries(material_pipeline_tests PRIVATE Rubia::Renderer)
    add_test(NAME material-pipeline-test COMMAND material_pipeline_tests)
    add_executable(pipeline_library_tests PipelineLibraryTests.cpp)
    target_link_libraries(pipeline_library_tests PRIVATE Rubia::Renderer)
    add_test(NAME pipeline-library-test COMMAND pipeline_library_tests)
    add_executable(resource_preparation_cpu_tests ResourcePreparationCpuTests.cpp)
    target_link_libraries(resource_preparation_cpu_tests PRIVATE Rubia::Renderer)
    add_test(NAME resource-preparation-cpu-test COMMAND resource_preparation_cpu_tests)
    add_executable(camera_clip_space_tests CameraClipSpaceTests.cpp)
    target_link_libraries(camera_clip_space_tests PRIVATE Rubia::Renderer)
    add_test(NAME camera-clip-space-test COMMAND camera_clip_space_tests)
    add_executable(frame_scheduler_tests FrameSchedulerTests.cpp)
    target_link_libraries(frame_scheduler_tests PRIVATE Rubia::Renderer)
    add_test(NAME frame-scheduler-test COMMAND frame_scheduler_tests)
    add_executable(presentation_frame_tests PresentationFrameTests.cpp)
    target_link_libraries(presentation_frame_tests PRIVATE Rubia::Renderer)
    add_test(NAME presentation-frame-test COMMAND presentation_frame_tests)
    add_executable(frame_pipeline_tests FramePipelineTests.cpp)
    target_link_libraries(frame_pipeline_tests PRIVATE Rubia::Renderer)
    add_test(NAME frame-pipeline-test COMMAND frame_pipeline_tests)
    add_executable(gui_backend_tests GuiBackendTests.cpp)
    target_link_libraries(gui_backend_tests PRIVATE Rubia::GuiIntegration)
    add_test(NAME gui-backend-test COMMAND gui_backend_tests)
    if(RUBIA_BUILD_APPS)
        add_executable(app_backend_tests AppBackendTests.cpp)
        target_link_libraries(app_backend_tests PRIVATE Rubia::AppRuntime glfw)
        add_test(NAME app-backend-test COMMAND app_backend_tests)
        set_tests_properties(app-backend-test PROPERTIES TIMEOUT 30)
        if(WIN32)
            add_custom_command(TARGET app_backend_tests POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Slang::Compiler>" "$<TARGET_FILE_DIR:app_backend_tests>"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:KTX::ktx>" "$<TARGET_FILE_DIR:app_backend_tests>")
        endif()
        add_executable(gui_layer_tests GuiLayerTests.cpp)
        target_link_libraries(gui_layer_tests PRIVATE Rubia::EditorGui)
        add_test(NAME gui-layer-test COMMAND gui_layer_tests)
        if(WIN32)
            add_custom_command(TARGET gui_layer_tests POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Slang::Compiler>" "$<TARGET_FILE_DIR:gui_layer_tests>")
        endif()
        add_executable(editor_theme_tests EditorThemeTests.cpp)
        target_link_libraries(editor_theme_tests PRIVATE Rubia::EditorCore Rubia::ImGuiCore)
        add_test(NAME editor-theme-test COMMAND editor_theme_tests)
        add_test(NAME asset-smoke-test
            COMMAND VulkanApp --asset-test
        )
        set_tests_properties(asset-smoke-test PROPERTIES
            WORKING_DIRECTORY "$<TARGET_FILE_DIR:VulkanApp>"
        )
        if(RUBIA_ENABLE_GPU_TESTS)
            add_executable(vulkan_presentation_tests VulkanPresentationTests.cpp)
            target_link_libraries(vulkan_presentation_tests PRIVATE Rubia::RendererVulkan)
            add_test(NAME vulkan-presentation-test COMMAND vulkan_presentation_tests)
            set_tests_properties(vulkan-presentation-test PROPERTIES
                FAIL_REGULAR_EXPRESSION "validation layer [(]error[)]|Validation Error"
                RUN_SERIAL TRUE TIMEOUT 60)
            if(NOT Vulkan_GLSLC_EXECUTABLE)
                message(FATAL_ERROR "GPU pipeline tests require the Vulkan SDK glslc compiler")
            endif()
            set(pipeline_test_spirv)
            foreach(shader pipeline_state.vert pipeline_state.frag draw_list.vert draw_list.frag draw_list_alternate.frag)
                set(source "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader}")
                set(output "${CMAKE_CURRENT_BINARY_DIR}/shaders/${shader}.spv")
                add_custom_command(OUTPUT "${output}"
                    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
                    COMMAND "${Vulkan_GLSLC_EXECUTABLE}" --target-env=vulkan1.0 "${source}" -o "${output}"
                    DEPENDS "${source}" VERBATIM)
                list(APPEND pipeline_test_spirv "${output}")
            endforeach()
            add_custom_target(pipeline_test_shaders DEPENDS ${pipeline_test_spirv})
            add_dependencies(rubia_smoke_tests pipeline_test_shaders)
            target_sources(rubia_smoke_tests PRIVATE GraphicsPipelineTests.cpp GraphicsPipelineCacheTests.cpp VulkanDrawListTests.cpp PipelinePreparationTests.cpp)
            target_compile_definitions(rubia_smoke_tests PRIVATE
                RUBIA_PIPELINE_STATE_TESTS
                RUBIA_PIPELINE_TEST_SHADER_DIR="${CMAKE_CURRENT_BINARY_DIR}/shaders")
            add_test(NAME startup-smoke-test COMMAND VulkanApp --startup-test)
            add_test(NAME runtime-smoke-test COMMAND VulkanApp --render-test)
            add_test(NAME editor-smoke-test COMMAND VulkanApp --editor-test)
            add_test(NAME preparation-gallery-test COMMAND VulkanApp --preparation-gallery-test)
            add_test(NAME editor-assets-test COMMAND VulkanApp --editor-assets-test)
            set_tests_properties(preparation-gallery-test startup-smoke-test runtime-smoke-test editor-smoke-test editor-assets-test
                PROPERTIES WORKING_DIRECTORY "$<TARGET_FILE_DIR:VulkanApp>"
                FAIL_REGULAR_EXPRESSION "validation layer [(]error[)]|Validation Error"
                RUN_SERIAL TRUE TIMEOUT 180)
        endif()
    endif()
endif()
