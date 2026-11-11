#include "core/DrawRecorder.hpp"
#include "render/RenderData.hpp"
namespace rubia::render
{
void recordDrawList(rhi::RenderEncoder& commands, const rhi::BindingSetRef& frameBindings,
                    uint32_t width, uint32_t height, const PreparedDrawList& draws)
{
    const rhi::PipelineResource* boundPipeline = nullptr;
    const MeshGeometry* boundMesh = nullptr;
    const rhi::BindingSetResource* boundMaterial = nullptr;
    const auto record = [&](const std::vector<PreparedDrawItem>& items) {
        for (const auto& item : items)
        {
            if (item.pipeline.get() != boundPipeline)
            {
                commands.bindPipeline(item.pipeline);
                commands.setViewport({0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1});
                commands.setScissor({0, 0, width, height});
                commands.bindResources(0, frameBindings);
                boundMaterial = nullptr;
                boundPipeline = item.pipeline.get();
            }
            if (item.mesh != boundMesh)
            {
                commands.bindVertexBuffer(0, item.mesh->vertexBuffer());
                if (item.mesh->indexBuffer()) commands.bindIndexBuffer(item.mesh->indexBuffer(), rhi::IndexType::UInt32);
                boundMesh = item.mesh;
            }
            if (item.material->descriptorSet().get() != boundMaterial)
            {
                commands.bindResources(1, item.material->descriptorSet());
                boundMaterial = item.material->descriptorSet().get();
            }
            DrawPushConstants constants{};
            constants.objectIndex = item.objectIndex;
            constants.alphaClipThreshold = item.material->renderState().alphaClipThreshold;
            commands.setConstants(rhi::shaderStageMask(rhi::ShaderStage::Vertex) |
                rhi::shaderStageMask(rhi::ShaderStage::Fragment), 0, sizeof(constants), &constants);
            const auto& submesh = item.mesh->submeshes()[item.submeshIndex];
            if (submesh.indexed()) commands.drawIndexed(submesh.indexCount, 1, submesh.firstIndex, 0, 0);
            else commands.draw(submesh.vertexCount, 1, submesh.firstVertex, 0);
        }
    };
    record(draws.opaque);
    record(draws.transparent);
}
} // namespace rubia::render
