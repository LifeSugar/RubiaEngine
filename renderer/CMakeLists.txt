add_library(rubia_renderer STATIC
    passes/FramePipeline.cpp
    passes/OutputBindings.cpp
    core/FrameScheduler.cpp
    core/DrawCompiler.cpp
    core/DrawRecorder.cpp
    resources/TextureResource.cpp
    resources/MeshGeometry.cpp
    resources/PreparedAssets.cpp
    resources/MaterialBinding.cpp
    resources/TextureUploadBuilder.cpp
    resources/RenderAssetStore.cpp
    resources/ResourcePreparationService.cpp
    resources/ResourcePreparations.cpp
    resources/ResourcePreparationSession.cpp
    resources/ScenePreparationSession.cpp
    pipeline/PipelineKey.cpp
    pipeline/PipelineLibrary.cpp
    pipeline/PipelinePreparation.cpp
    core/FrameBindings.cpp
    core/FrameBindings.hpp
    pipeline/MaterialPipelineCompiler.cpp
    pipeline/MaterialPipelineCompiler.hpp
)
add_library(Rubia::Renderer ALIAS rubia_renderer)
target_include_directories(rubia_renderer PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(rubia_renderer PUBLIC Rubia::Engine Rubia::Rhi)

if(RUBIA_BUILD_VULKAN)
    add_subdirectory(vulkan)
endif()
