#pragma once

#include "asset/AssetFwd.hpp"
#include "asset/ShaderInterface.hpp"

#include <optional>
#include <string>
#include <vector>

namespace rubia::asset
{

using ShaderStageMask = uint32_t;
[[nodiscard]] constexpr ShaderStageMask shaderStageMask(ShaderStage stage) noexcept
{
    return 1u << static_cast<uint32_t>(stage);
}

struct ProgramResourceBinding
{
    uint32_t set = 0;
    uint32_t binding = 0;
    ShaderResourceType type = ShaderResourceType::UniformBuffer;
    uint32_t arrayCount = 1;
    ShaderStageMask stages = 0;
    // Different stages may use different names for the same binding.
    std::vector<std::string> names;
    std::optional<ShaderParameterBlockDesc> bufferLayout;
};

struct ProgramPushConstantRange
{
    uint32_t offset = 0;
    uint32_t size = 0;
    ShaderStageMask stages = 0;
};

struct ShaderProgramInterface
{
    std::vector<ProgramResourceBinding> bindings;
    std::vector<ProgramPushConstantRange> pushConstants;
    std::vector<ShaderStageIoDesc> vertexInputs;
    std::vector<ShaderStageIoDesc> fragmentOutputs;
};

class AssetManager;
class ShaderProgramBuilder;

/// Immutable CPU graphics program. Shader assets own the bytecode; this asset
/// owns the validated, merged interface. No Vulkan or importer dependencies.
class ShaderProgramAsset final
{
  public:
    struct CreateInfo
    {
        std::string name;
        std::vector<ShaderAssetHandle> shaders;
    };

    [[nodiscard]] const std::string &name() const noexcept { return name_; }
    [[nodiscard]] const std::vector<ShaderAssetHandle> &shaders() const noexcept
    {
        return shaders_;
    }
    [[nodiscard]] const ShaderProgramInterface &interface() const noexcept { return interface_; }
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return shaders_.size() == 2;
    }
    [[nodiscard]] uint64_t codeSignature() const noexcept { return codeSignature_; }
    [[nodiscard]] uint64_t layoutSignature() const noexcept { return layoutSignature_; }
    [[nodiscard]] uint64_t interfaceSignature() const noexcept { return interfaceSignature_; }

  private:
    friend class ShaderProgramBuilder;
    ShaderProgramAsset() = default;
    std::string name_;
    std::vector<ShaderAssetHandle> shaders_;
    ShaderProgramInterface interface_;
    uint64_t codeSignature_ = 0;
    uint64_t layoutSignature_ = 0;
    uint64_t interfaceSignature_ = 0;
};

class ShaderProgramBuilder final
{
  public:
    [[nodiscard]] static ShaderProgramAsset build(ShaderProgramAsset::CreateInfo createInfo,
                                                  const AssetManager &assets);
};

} // namespace rubia::asset
