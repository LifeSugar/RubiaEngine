#include "asset/ShaderAsset.hpp"

#include <stdexcept>
#include <utility>

namespace rubia::asset
{

ShaderAsset::ShaderAsset(CreateInfo createInfo)
{
    create(std::move(createInfo));
}

void ShaderAsset::create(CreateInfo createInfo)
{
    if (createInfo.entryPoint.empty())
    {
        throw std::invalid_argument("shader entry point must not be empty");
    }
    if (createInfo.artifact.bytes.empty() ||
        !compatibleShaderFormat(createInfo.artifact.target.backend, createInfo.artifact.format))
    {
        throw std::invalid_argument("shader artifact is empty or incompatible with its target");
    }
    if (createInfo.interface.signature == 0 ||
        createInfo.interface.stage != createInfo.stage ||
        createInfo.interface.entryPoint != createInfo.entryPoint)
    {
        throw std::invalid_argument(
            "shader reflection does not match its stage and entry point");
    }

    name_ = std::move(createInfo.name);
    stage_ = createInfo.stage;
    entryPoint_ = std::move(createInfo.entryPoint);
    artifact_ = std::move(createInfo.artifact);
    interface_ = std::move(createInfo.interface);
}

void ShaderAsset::reset() noexcept
{
    name_.clear();
    stage_ = ShaderStage::Vertex;
    entryPoint_ = "main";
    artifact_ = {};
    interface_ = {};
}

} // namespace rubia::asset
