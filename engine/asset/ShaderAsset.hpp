#pragma once

#include "asset/ShaderInterface.hpp"
#include "rubia/ShaderArtifact.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace rubia::asset
{

/// Source-independent ownership of compiled code and its target-specific reflection.
class ShaderAsset final
{
public:
    struct CreateInfo
    {
        std::string name;
        ShaderStage stage = ShaderStage::Vertex;
        std::string entryPoint = "main";
        ShaderArtifact artifact;
        ShaderInterface interface;
    };

    ShaderAsset() = default;
    explicit ShaderAsset(CreateInfo createInfo);

    void create(CreateInfo createInfo);
    void reset() noexcept;

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] ShaderStage stage() const noexcept { return stage_; }
    [[nodiscard]] const std::string& entryPoint() const noexcept { return entryPoint_; }
    [[nodiscard]] const ShaderArtifact& artifact() const noexcept { return artifact_; }
    [[nodiscard]] const ShaderInterface& interface() const noexcept
    {
        return interface_;
    }
    [[nodiscard]] explicit operator bool() const noexcept { return !artifact_.bytes.empty(); }

private:
    std::string name_;
    ShaderStage stage_ = ShaderStage::Vertex;
    std::string entryPoint_ = "main";
    ShaderArtifact artifact_;
    ShaderInterface interface_;
};

} // namespace rubia::asset
