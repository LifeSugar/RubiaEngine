#pragma once
#include "asset/MaterialTemplateAsset.hpp"
#include "asset/ShaderAsset.hpp"
#include "vulkan/DescriptorSetLayout.hpp"
#include "vulkan/DescriptorPool.hpp"
#include <memory>

namespace rubia::rhi::vulkan
{
class Device;
// Device-created objects; none of these require a transfer command or staging buffer.
class GpuShader final
{
public:
    GpuShader(const Device& device, std::shared_ptr<const asset::ShaderAsset> source);
    ~GpuShader();
    GpuShader(const GpuShader&) = delete;
    GpuShader& operator=(const GpuShader&) = delete;
    VkShaderModule module() const noexcept
    {
        return module_;
    }
    VkDevice device() const noexcept { return device_; }
    VkPipelineShaderStageCreateInfo stageInfo() const;
    const asset::ShaderAsset& source() const
    {
        return *source_;
    }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkShaderModule module_ = VK_NULL_HANDLE;
    std::shared_ptr<const asset::ShaderAsset> source_;
};
class GpuShaderProgram final
{
public:
    GpuShaderProgram(const Device& device, std::shared_ptr<const asset::ShaderProgramAsset> source,
                     std::vector<std::shared_ptr<const GpuShader>> shaders);
    ~GpuShaderProgram();
    GpuShaderProgram(const GpuShaderProgram&) = delete;
    GpuShaderProgram& operator=(const GpuShaderProgram&) = delete;
    const asset::ShaderProgramAsset& source() const
    {
        return *source_;
    }
    std::vector<VkPipelineShaderStageCreateInfo> stages() const;
    std::vector<VkDescriptorSetLayout> setLayouts() const;
    std::vector<std::shared_ptr<const DescriptorSetLayoutState>> setLayoutReferences() const;
    const std::vector<std::shared_ptr<const GpuShader>>& shaders() const noexcept { return shaders_; }
    VkDevice device() const noexcept { return device_; }
    VkPipelineLayout layout() const noexcept
    {
        return layout_;
    }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    std::shared_ptr<const asset::ShaderProgramAsset> source_;
    std::vector<std::shared_ptr<const GpuShader>> shaders_;
    std::vector<DescriptorSetLayout> sets_;
};
class GpuMaterialTemplate final
{
public:
    GpuMaterialTemplate(const Device& device,
                        std::shared_ptr<const asset::MaterialTemplateAsset> source,
                        std::shared_ptr<const GpuShaderProgram> program);
    /// Allocates a pool sized for one material binding version.
    [[nodiscard]] DescriptorPool createDescriptorPool(const Device& device) const;
    VkDescriptorSetLayout layout() const noexcept
    {
        return layout_.get();
    }
    std::shared_ptr<const DescriptorSetLayoutState> layoutReference() const noexcept
    {
        return layout_.reference();
    }
    const asset::MaterialTemplateAsset& source() const
    {
        return *source_;
    }
    const std::shared_ptr<const GpuShaderProgram>& program() const
    {
        return program_;
    }

private:
    std::shared_ptr<const asset::MaterialTemplateAsset> source_;
    std::shared_ptr<const GpuShaderProgram> program_;
    DescriptorSetLayout layout_;
};
} // namespace rubia::rhi::vulkan
