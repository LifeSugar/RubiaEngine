#pragma once
#include "rubia/ShaderArtifact.hpp"
#include <slang.h>
#include <stdexcept>

namespace rubia::importer::shader::detail
{
struct SlangTarget
{
    ShaderTarget target;
    ShaderFormat format;
    SlangCompileTarget slangFormat;
    const char* argument;
};
inline SlangTarget resolveSlangTarget(ShaderTarget target)
{
    SlangTarget result;
    const char* defaultProfile = nullptr;
    switch (target.backend)
    {
    case ShaderBackend::Vulkan:
        result.format = ShaderFormat::SpirV; result.slangFormat = SLANG_SPIRV;
        result.argument = "spirv"; defaultProfile = "spirv_1_3"; break;
    case ShaderBackend::Direct3D12:
        result.format = ShaderFormat::Dxil; result.slangFormat = SLANG_DXIL;
        result.argument = "dxil"; defaultProfile = "sm_6_0"; break;
    case ShaderBackend::Metal:
        result.format = ShaderFormat::MetalSource; result.slangFormat = SLANG_METAL;
        result.argument = "metal"; defaultProfile = "metallib_2_3"; break;
    case ShaderBackend::OpenGL:
        result.format = ShaderFormat::Glsl; result.slangFormat = SLANG_GLSL;
        result.argument = "glsl"; defaultProfile = "glsl_460"; break;
    default: throw std::invalid_argument("Unknown shader target backend");
    }
    if (target.profile.empty()) target.profile = defaultProfile;
    result.target = std::move(target);
    return result;
}
}
