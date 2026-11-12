add_library(rubia_engine STATIC)
add_library(Rubia::Engine ALIAS rubia_engine)

target_include_directories(rubia_engine PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_compile_definitions(rubia_engine PUBLIC
    GLM_FORCE_DEPTH_ZERO_TO_ONE
)
target_link_libraries(rubia_engine
    PUBLIC
        Rubia::ShaderTypes
        glm::glm
        rubia_project_options
)

add_subdirectory(asset)
add_subdirectory(math)
add_subdirectory(render)
add_subdirectory(scene)
