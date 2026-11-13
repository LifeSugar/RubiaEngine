#pragma once
#include "asset/MaterialTemplateAsset.hpp"
#include "asset/ShaderAsset.hpp"
#include "rhi/Device.hpp"
namespace rubia::render
{
rhi::ShaderStage shaderStage(asset::ShaderStage);
rhi::ShaderStageMask shaderStages(asset::ShaderStageMask);
rhi::BindingLayoutDesc bindingLayout(const std::vector<asset::ProgramResourceBinding> &);
class PreparedShader final
{
  public:
    PreparedShader(const rhi::Device &, std::shared_ptr<const asset::ShaderAsset>);
    const rhi::ShaderRef &resource() const noexcept { return shader_; }
    rhi::DeviceId device() const noexcept { return shader_->deviceId(); }
    const asset::ShaderAsset &source() const { return *source_; }

  private:
    std::shared_ptr<const asset::ShaderAsset> source_;
    rhi::ShaderRef shader_;
};
class PreparedShaderProgram final
{
  public:
    PreparedShaderProgram(const rhi::Device &, std::shared_ptr<const asset::ShaderProgramAsset>,
                          std::vector<std::shared_ptr<const PreparedShader>>);
    const asset::ShaderProgramAsset &source() const { return *source_; }
    const std::vector<rhi::BindingLayoutRef> &setLayoutReferences() const noexcept { return sets_; }
    const std::vector<std::shared_ptr<const PreparedShader>> &shaders() const noexcept
    {
        return shaders_;
    }
    const std::vector<rhi::ConstantRangeDesc> &constantRanges() const noexcept
    {
        return constants_;
    }
    rhi::DeviceId device() const noexcept { return device_; }

  private:
    rhi::DeviceId device_;
    std::shared_ptr<const asset::ShaderProgramAsset> source_;
    std::vector<std::shared_ptr<const PreparedShader>> shaders_;
    std::vector<rhi::BindingLayoutRef> sets_;
    std::vector<rhi::ConstantRangeDesc> constants_;
};
class PreparedMaterialTemplate final
{
  public:
    PreparedMaterialTemplate(const rhi::Device &,
                             std::shared_ptr<const asset::MaterialTemplateAsset>,
                             std::shared_ptr<const PreparedShaderProgram>);
    const rhi::BindingLayoutRef &layout() const noexcept { return layout_; }
    const rhi::BindingLayoutRef &layoutReference() const noexcept { return layout_; }
    const asset::MaterialTemplateAsset &source() const { return *source_; }
    const std::shared_ptr<const PreparedShaderProgram> &program() const noexcept
    {
        return program_;
    }

  private:
    std::shared_ptr<const asset::MaterialTemplateAsset> source_;
    std::shared_ptr<const PreparedShaderProgram> program_;
    rhi::BindingLayoutRef layout_;
};
} // namespace rubia::render
