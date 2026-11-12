add_library(rubia_shader_types INTERFACE)
add_library(Rubia::ShaderTypes ALIAS rubia_shader_types)
target_include_directories(rubia_shader_types INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(rubia_shader_types INTERFACE rubia_project_options)
