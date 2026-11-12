target_sources(rubia_importers PRIVATE
    ExternalShaderCompiler.cpp
    ExternalShaderCompiler.hpp
    ShaderInterfaceSignature.cpp
    ShaderInterfaceSignature.hpp
    HlslShaderCompiler.cpp
    HlslShaderCompiler.hpp
    SlangShaderCompiler.cpp
    SlangShaderCompiler.hpp
    SpirvReflection.cpp
    SpirvReflection.hpp
    SpirvShaderImporter.cpp
    SpirvShaderImporter.hpp
)

target_link_libraries(rubia_importers PRIVATE Slang::Compiler)

configure_file(SlangCompilerConfig.hpp.in SlangCompilerConfig.hpp @ONLY)
target_include_directories(rubia_importers PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
