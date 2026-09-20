add_library(rubia_editor STATIC
    App.cpp
    AppContentLoading.cpp
    App.hpp
    ApplicationGui.hpp
    AppResize.cpp
    AppTextureReimport.cpp
    EditorLayer.cpp
    EditorLayer.hpp
    EditorTheme.cpp
    EditorTheme.hpp
    EditorFwd.hpp
    EditorSelection.cpp
    EditorSelection.hpp
    ImGuiLayer.cpp
    ImGuiLayer.hpp
    RuntimeGui.cpp
    RuntimeGui.hpp
    content/DemoContent.cpp
    content/DemoContent.hpp
    inspectors/InspectorWidgets.cpp
    inspectors/InspectorWidgets.hpp
    inspectors/MaterialInspector.cpp
    inspectors/MaterialInspector.hpp
    inspectors/ModelInspector.cpp
    inspectors/ModelInspector.hpp
    inspectors/SceneNodeInspector.cpp
    inspectors/SceneNodeInspector.hpp
    inspectors/TextureInspector.cpp
    inspectors/TextureInspector.hpp
    panels/AssetBrowserPanel.cpp
    panels/AssetBrowserPanel.hpp
    panels/ConsolePanel.cpp
    panels/ConsolePanel.hpp
    content/ContentLoadStatus.hpp
    panels/InspectorPanel.cpp
    panels/InspectorPanel.hpp
    panels/PreferencesPanel.cpp
    panels/PreferencesPanel.hpp
    panels/SceneHierarchyPanel.cpp
    panels/SceneHierarchyPanel.hpp
    panels/TransformPanel.cpp
    panels/TransformPanel.hpp
)
add_library(Rubia::Editor ALIAS rubia_editor)

target_include_directories(rubia_editor PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_compile_definitions(rubia_editor PRIVATE
    PROJECT_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)
target_link_libraries(rubia_editor
    PUBLIC
        Rubia::Engine
        Rubia::Importers
        Rubia::RhiVulkan
        rubia_project_options
    PRIVATE
        imgui
)

add_executable(VulkanApp
    EditorApp.cpp
    EditorApp.hpp
    main.cpp
)
target_link_libraries(VulkanApp PRIVATE Rubia::Editor)

# Runtime content is copied as one coherent tree, matching the source layout.
add_custom_command(TARGET VulkanApp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${PROJECT_SOURCE_DIR}/assets"
            "$<TARGET_FILE_DIR:VulkanApp>/assets"
    COMMENT "Copying runtime assets"
)

# KTX is a shared vcpkg dependency on Windows. Keep its runtime dependencies
# beside the executable so launches and smoke tests do not depend on PATH.
if(WIN32)
    add_custom_command(TARGET VulkanApp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:KTX::ktx>"
                "$<TARGET_FILE_DIR:VulkanApp>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:zstd::libzstd_shared>"
                "$<TARGET_FILE_DIR:VulkanApp>"
        COMMENT "Copying KTX runtime dependencies"
    )
endif()
