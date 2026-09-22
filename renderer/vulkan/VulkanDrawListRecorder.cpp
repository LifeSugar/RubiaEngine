#include "vulkan/VulkanDrawListRecorder.hpp"
#include "render/RenderData.hpp"
#include "vulkan/GpuMaterial.hpp"
#include "vulkan/GraphicsPipeline.hpp"
#include "vulkan/Mesh.hpp"
#include "vulkan/VulkanDrawList.hpp"

namespace rubia::rhi::vulkan
{
void recordVulkanDrawList(VkCommandBuffer commandBuffer, VkDescriptorSet frameDescriptorSet,
                          VkExtent2D extent, const VulkanDrawList &draws)
{
    const GraphicsPipeline *boundPipeline = nullptr;
    const Mesh *boundMesh = nullptr;
    VkDescriptorSet boundMaterial = VK_NULL_HANDLE;
    const VkViewport viewport{
        0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height), 0, 1};
    const VkRect2D scissor{{0, 0}, extent};
    auto record = [&](const std::vector<VulkanDrawItem> &items) {
        for (const auto &item : items)
        {
            const auto &pipeline = *item.pipeline;
            if (&pipeline != boundPipeline)
            {
                vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.get());
                // Re-establish dynamic state after a preceding static-state pipeline too.
                vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
                vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
                // Different material programs may have incompatible pipeline layouts.
                // Rebinding both sets also handles descriptor-set disturbance rules.
                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        pipeline.layout(), 0, 1, &frameDescriptorSet, 0, nullptr);
                boundMaterial = VK_NULL_HANDLE;
                boundPipeline = &pipeline;
            }
            if (item.mesh != boundMesh)
            {
                item.mesh->bind(commandBuffer);
                boundMesh = item.mesh;
            }
            const auto materialSet = item.material->descriptorSet();
            if (materialSet != boundMaterial)
            {
                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        pipeline.layout(), 1, 1, &materialSet, 0, nullptr);
                boundMaterial = materialSet;
            }
            render::DrawPushConstants constants{};
            constants.objectIndex = item.objectIndex;
            constants.alphaClipThreshold = item.material->renderState().alphaClipThreshold;
            vkCmdPushConstants(commandBuffer, pipeline.layout(),
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                               sizeof(constants), &constants);
            const auto &submesh = item.mesh->submeshes()[item.submeshIndex];
            if (submesh.indexed())
                vkCmdDrawIndexed(commandBuffer, submesh.indexCount, 1, submesh.firstIndex, 0, 0);
            else
                vkCmdDraw(commandBuffer, submesh.vertexCount, 1, submesh.firstVertex, 0);
        }
    };
    record(draws.opaque);
    record(draws.transparent);
}
} // namespace rubia::rhi::vulkan
