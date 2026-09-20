#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace rubia::rhi::vulkan
{

class Device;
class GpuShaderProgram;

/// Owns a graphics pipeline and its pipeline layout.
class GraphicsPipeline final
{
public:
    /// Parameters used to configure a graphics pipeline.
    struct CreateInfo
    {
        /// Render pass whose subpass layout the pipeline targets.
        VkRenderPass renderPass = VK_NULL_HANDLE;
        /// Reuses prepared shader modules when supplied; SPIR-V fields are the fallback.
        std::shared_ptr<const GpuShaderProgram> program;
        /// CPU-owned SPIR-V imported by the Asset layer.
        std::vector<uint32_t> vertexShaderSpirv;
        std::string vertexEntryPoint = "main";
        /// CPU-owned SPIR-V imported by the Asset layer.
        std::vector<uint32_t> fragmentShaderSpirv;
        std::string fragmentEntryPoint = "main";
        /// Descriptor set layouts exposed through the pipeline layout.
        std::vector<VkDescriptorSetLayout> descriptorSetLayouts;
        /// Push-constant ranges exposed through the pipeline layout.
        std::vector<VkPushConstantRange> pushConstantRanges;
        /// Vertex-buffer bindings consumed by the vertex shader.
        std::vector<VkVertexInputBindingDescription> vertexBindings;
        /// Vertex attributes consumed by the vertex shader.
        std::vector<VkVertexInputAttributeDescription> vertexAttributes;
        /// Primitive topology consumed by the input-assembly stage.
        VkPrimitiveTopology primitiveTopology =
            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        /// Rasterized faces discarded before fragment shading.
        VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
        /// Winding order treated as front-facing.
        VkFrontFace frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        /// Enables depth comparisons when the subpass has a depth attachment.
        VkBool32 depthTestEnable = VK_TRUE;
        /// Enables writes to the depth attachment.
        VkBool32 depthWriteEnable = VK_TRUE;
        /// Comparison operation used by the depth test.
        VkCompareOp depthCompareOp = VK_COMPARE_OP_LESS;
        /// Rasterization sample count; must match the target subpass.
        VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT;
    };

    /// Creates an empty graphics-pipeline wrapper.
    GraphicsPipeline() = default;
    /// Creates a graphics pipeline from the supplied settings.
    GraphicsPipeline(const Device& device, const CreateInfo& createInfo);
    /// Destroys the owned pipeline and pipeline layout.
    ~GraphicsPipeline();

    GraphicsPipeline(const GraphicsPipeline&) = delete;
    GraphicsPipeline& operator=(const GraphicsPipeline&) = delete;

    /// Transfers pipeline ownership from another wrapper.
    GraphicsPipeline(GraphicsPipeline&& other) noexcept;
    /// Replaces this pipeline by taking ownership from another wrapper.
    GraphicsPipeline& operator=(GraphicsPipeline&& other) noexcept;

    /// Creates or replaces the graphics pipeline and its layout.
    void create(const Device& device, const CreateInfo& createInfo);
    /// Destroys the pipeline and layout and clears their state.
    void reset() noexcept;

    /// Returns the owned Vulkan graphics-pipeline handle.
    [[nodiscard]] VkPipeline get() const noexcept { return pipeline_; }
    /// Returns the pipeline layout used for descriptor and push-constant binding.
    [[nodiscard]] VkPipelineLayout layout() const noexcept { return layout_; }
    /// Returns whether a graphics pipeline is currently owned.
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return pipeline_ != VK_NULL_HANDLE;
    }

private:
    /// Logical device that owns the pipeline resources.
    VkDevice device_ = VK_NULL_HANDLE;
    /// Owned Vulkan pipeline-layout handle.
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    /// Owned Vulkan graphics-pipeline handle.
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};

} // namespace rubia::rhi::vulkan
