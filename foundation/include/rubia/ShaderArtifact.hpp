#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <stdexcept>
#include <vector>
#include <utility>

namespace rubia
{
enum class ShaderBackend { Vulkan, Direct3D12, Metal, OpenGL };
enum class ShaderFormat { SpirV, Dxil, MetalSource, Glsl };

struct ShaderTarget
{
    ShaderBackend backend = ShaderBackend::Vulkan;
    // Empty on a compile request selects the compiler's default for this backend.
    std::string profile;
    friend bool operator==(const ShaderTarget& a, const ShaderTarget& b)
    { return a.backend == b.backend && a.profile == b.profile; }
    friend bool operator!=(const ShaderTarget& a, const ShaderTarget& b) { return !(a == b); }
};

// A target-specific compiled artifact. Text and binary outputs share the same
// ownership model; native validation/translation belongs to the consuming backend.
struct ShaderArtifact
{
    ShaderTarget target;
    ShaderFormat format = ShaderFormat::SpirV;
    std::vector<uint8_t> bytes;

    static ShaderArtifact copy(ShaderTarget target, ShaderFormat format,
                               const void* data, size_t size)
    {
        if (size && !data) throw std::invalid_argument("null shader artifact data");
        ShaderArtifact result{std::move(target), format, std::vector<uint8_t>(size)};
        if (size) std::memcpy(result.bytes.data(), data, size);
        return result;
    }
};
inline bool compatibleShaderFormat(ShaderBackend backend, ShaderFormat format) noexcept
{
    switch (backend)
    {
    case ShaderBackend::Vulkan: return format == ShaderFormat::SpirV;
    case ShaderBackend::Direct3D12: return format == ShaderFormat::Dxil;
    case ShaderBackend::Metal: return format == ShaderFormat::MetalSource;
    case ShaderBackend::OpenGL: return format == ShaderFormat::Glsl;
    }
    return false;
}
}
