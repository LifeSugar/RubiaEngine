add_library(rubia_smoke_tests STATIC
    AppSmokeTests.cpp
    AppStartupTests.cpp
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

# The editor keeps its existing command-line smoke-test entry points.
target_link_libraries(VulkanApp PRIVATE rubia_smoke_tests)

if(BUILD_TESTING)
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
        add_test(NAME startup-smoke-test COMMAND VulkanApp --startup-test)
        add_test(NAME runtime-smoke-test COMMAND VulkanApp --render-test)
        add_test(NAME editor-smoke-test COMMAND VulkanApp --editor-test)
        set_tests_properties(startup-smoke-test runtime-smoke-test editor-smoke-test
            PROPERTIES WORKING_DIRECTORY "$<TARGET_FILE_DIR:VulkanApp>"
            RUN_SERIAL TRUE TIMEOUT 180)
    endif()
endif()
