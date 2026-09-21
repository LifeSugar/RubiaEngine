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
        Rubia::Editor
        rubia_project_options
    PRIVATE
        imgui
        KTX::ktx
)

target_compile_definitions(rubia_smoke_tests PRIVATE PROJECT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

# The editor keeps its existing command-line smoke-test entry points.
target_link_libraries(VulkanApp PRIVATE rubia_smoke_tests)

if(BUILD_TESTING)
    add_executable(asset_revision_tests AssetRevisionTests.cpp)
    target_link_libraries(asset_revision_tests PRIVATE Rubia::Engine rubia_project_options)
    add_test(NAME asset-revision-test COMMAND asset_revision_tests)
    add_executable(editor_theme_tests EditorThemeTests.cpp)
    target_link_libraries(editor_theme_tests PRIVATE Rubia::Editor imgui)
    add_test(NAME editor-theme-test COMMAND editor_theme_tests)
    add_executable(shader_program_tests ShaderProgramTests.cpp)
    target_link_libraries(shader_program_tests PRIVATE Rubia::Engine rubia_project_options)
    add_test(NAME shader-program-test COMMAND shader_program_tests)
    add_test(NAME asset-smoke-test
        COMMAND VulkanApp --asset-test
    )
    set_tests_properties(asset-smoke-test PROPERTIES
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:VulkanApp>"
    )
    option(RUBIA_ENABLE_GPU_TESTS "Register hidden-window Vulkan smoke tests" OFF)
    if(RUBIA_ENABLE_GPU_TESTS)
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
