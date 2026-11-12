configure_file(EditorBuildInfo.hpp.in generated/EditorBuildInfo.hpp @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/assets/branding/rubia-icon-64.png")
file(READ "${PROJECT_SOURCE_DIR}/assets/branding/rubia-icon-64.png" RUBIA_ICON_HEX HEX)
string(REGEX REPLACE "(..)" "0x\\1," RUBIA_ICON_BYTES "${RUBIA_ICON_HEX}")
configure_file(EditorIconData.hpp.in generated/EditorIconData.hpp @ONLY)

# Editor business code has no graphics-backend or native-window dependency.
add_library(rubia_editor_core STATIC
    ApplicationGui.hpp
    EditorLayer.cpp
    EditorLayer.hpp
    EditorTheme.cpp
    EditorTheme.hpp
    EditorFwd.hpp
    EditorSelection.cpp
    EditorSelection.hpp
    RuntimeGui.cpp
    RuntimeGui.hpp
    content/EditorAssetController.cpp
    content/EditorAssetController.hpp
    panels/MaterialAuthoringPanel.cpp
    panels/MaterialAuthoringPanel.hpp
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
add_library(Rubia::EditorCore ALIAS rubia_editor_core)
target_include_directories(rubia_editor_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(rubia_editor_core PRIVATE PROJECT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(rubia_editor_core PUBLIC
    Rubia::Engine
    Rubia::Importers
    Rubia::ImGuiCore
    rubia_project_options
)

# UI context/lifecycle depends only on injected GUI backends.
add_library(rubia_editor_gui STATIC ImGuiLayer.cpp ImGuiLayer.hpp)
add_library(Rubia::EditorGui ALIAS rubia_editor_gui)
target_include_directories(rubia_editor_gui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(rubia_editor_gui PRIVATE PROJECT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(rubia_editor_gui PUBLIC Rubia::EditorCore Rubia::GuiIntegration Rubia::ImGuiCore)

# The application host remains the composition root for concrete backends.
# Keep the old target/alias while existing application tests migrate.
add_library(rubia_editor STATIC
    App.cpp
    AppContentLoading.cpp
    App.hpp
    AppResize.cpp
    AppTextureReimport.cpp
    content/DemoContent.cpp
    content/DemoContent.hpp
)
add_library(Rubia::AppRuntime ALIAS rubia_editor)
add_library(Rubia::Editor ALIAS rubia_editor)
target_include_directories(rubia_editor PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(rubia_editor PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_definitions(rubia_editor PRIVATE PROJECT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(rubia_editor
    PUBLIC
        Rubia::EditorCore
        Rubia::EditorGui
        Rubia::RendererVulkan
        Rubia::ImGuiVulkanAdapter
        rubia_project_options
    PRIVATE
        Rubia::ImGuiGlfwAdapter
)

add_executable(VulkanApp
    EditorApp.cpp
    EditorApp.hpp
    main.cpp
)
target_link_libraries(VulkanApp PRIVATE Rubia::AppRuntime)
if(WIN32)
    enable_language(RC)
    configure_file(RubiaEngine.rc.in generated/RubiaEngine.rc @ONLY)
    target_sources(VulkanApp PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated/RubiaEngine.rc")
endif()

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
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Slang::Compiler>" "$<TARGET_FILE_DIR:VulkanApp>")
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
