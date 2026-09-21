#pragma once

#include "vulkan/GraphicsPipelineState.hpp"
#include "vulkan/DescriptorSetLayout.hpp"
#include "vulkan/RenderPass.hpp"

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
    /// Value-owned VS/optional-FS description with immutable GPU dependency owners.
    struct CreateInfo
    {
        /// Render pass whose subpass layout the pipeline targets.
        std::shared_ptr<const RenderPassState> renderPass;
        uint32_t subpass = 0;
        /// Reuses prepared shader modules when supplied; SPIR-V fields are the fallback.
        std::shared_ptr<const GpuShaderProgram> program;
        /// CPU-owned SPIR-V imported by the Asset layer.
        std::vector<uint32_t> vertexShaderSpirv;
        std::string vertexEntryPoint = "main";
        /// Empty permits a vertex-only pipeline, e.g. a depth-only pass.
        std::vector<uint32_t> fragmentShaderSpirv;
        std::string fragmentEntryPoint = "main";
        /// Per-stage specialization values; ordinary uniforms are not PSO state.
        std::vector<PipelineStageSpecialization> specializations;
        /// Descriptor set layouts exposed through the pipeline layout.
        std::vector<std::shared_ptr<const DescriptorSetLayoutState>> descriptorSetLayouts;
        /// Push-constant ranges exposed through the pipeline layout.
        std::vector<VkPushConstantRange> pushConstantRanges;
        /// Vertex-buffer bindings consumed by the vertex shader.
        std::vector<VkVertexInputBindingDescription> vertexBindings;
        /// Vertex attributes consumed by the vertex shader.
        std::vector<VkVertexInputAttributeDescription> vertexAttributes;
        PipelineInputAssemblyState inputAssembly;
        PipelineRasterizationState rasterization;
        PipelineDepthStencilState depthStencil;
        PipelineMultisampleState multisample;
        PipelineColorBlendState colorBlend;
        PipelineViewportState viewport;
        /// Core Vulkan 1.0 dynamic states only. Caller must set them before draw.
        std::vector<VkDynamicState> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
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
    /// Also used before cache lookup so a hit cannot bypass request validation.
    static void validate(const Device& device, const CreateInfo& createInfo);
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
    std::shared_ptr<const RenderPassState> renderPass_;
    std::vector<std::shared_ptr<const DescriptorSetLayoutState>> descriptorSetLayouts_;
};

} // namespace rubia::rhi::vulkan
