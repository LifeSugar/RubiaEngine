add_library(rubia_rhi INTERFACE)
add_library(Rubia::Rhi ALIAS rubia_rhi)
target_include_directories(rubia_rhi INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(rubia_rhi INTERFACE rubia_project_options)
