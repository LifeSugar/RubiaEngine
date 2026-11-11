#include "core/DrawCompiler.hpp"
#include "pipeline/MaterialPipelineCompiler.hpp"
#include "render/MaterialKey.hpp"
#include "render/RenderQueue.hpp"
#include <map>
#include <stdexcept>

namespace rubia::render
{
namespace
{
void validatePass(const rhi::GraphicsPipelineDesc& pass)
{
    if (pass.bindingLayouts.empty() || !pass.bindingLayouts.front())
        throw std::invalid_argument("scene draw compilation requires a frame binding layout");
    if (!pass.specializations.empty())
        throw std::invalid_argument("scene specialization needs a per-program variant contract");
    for (auto state : pass.dynamicStates)
        if (state != rhi::DynamicState::Viewport && state != rhi::DynamicState::Scissor)
            throw std::invalid_argument("scene recorder only supplies dynamic viewport/scissor");
}
using Resolved = std::map<const PreparedMaterialTemplate*,
    std::map<PipelineVariantKey, rhi::PipelineRef, PipelineVariantKeyLess>>;

std::vector<PreparedDrawItem> compileItems(const std::vector<RenderItem>& source,
    std::size_t objectCount, bool transparent, const RenderAssetStore& assets,
    const rhi::GraphicsPipelineDesc& pass, PipelineLibrary& pipelines, Resolved& resolved)
{
    std::vector<PreparedDrawItem> result;
    result.reserve(source.size());
    for (const auto& item : source)
    {
        if (!item.mesh || !item.material || !item.materialKey || !item.pipelineKey ||
            item.materialKey != makeMaterialKey(item.material) || item.objectIndex >= objectCount ||
            isTransparentQueue(item.queue) != transparent)
            throw std::invalid_argument("RenderList contains an invalid draw item");
        const auto* mesh = assets.tryMesh(item.mesh);
        const auto* material = assets.tryMaterial(item.material);
        if (!mesh || !material) throw std::invalid_argument("RenderList references a non-resident resource");
        if (item.submeshIndex >= mesh->submeshes().size())
            throw std::out_of_range("RenderList references an invalid submesh");
        const auto& state = material->renderState();
        if (material->materialTemplate() != item.pipelineKey.materialTemplate ||
            renderQueueFor(state) != item.queue ||
            makePipelineVariantKey(item.pipelineKey.materialTemplate, state) != item.pipelineKey)
            throw std::invalid_argument("RenderList material state does not match its semantic keys");
        auto materialTemplate = assets.materialTemplateForMaterial(item.material);
        if (!materialTemplate) throw std::invalid_argument("resident material has no prepared template");
        auto& variants = resolved[materialTemplate.get()];
        auto found = variants.find(item.pipelineKey);
        if (found == variants.end())
            found = variants.emplace(item.pipelineKey, pipelines.getOrCreate(
                DrawCompiler::describeMaterialPipeline(*materialTemplate, item.pipelineKey, pass))).first;
        result.push_back({mesh, material, item.pipelineKey, found->second, item.submeshIndex, item.objectIndex});
    }
    return result;
}
} // namespace

rhi::GraphicsPipelineDesc DrawCompiler::describeMaterialPipeline(
    const PreparedMaterialTemplate& material, const PipelineVariantKey& key,
    const rhi::GraphicsPipelineDesc& pass)
{
    validatePass(pass);
    MaterialPipelineInput input{material.program()->source().interface()};
    input.materialBindingGroup = material.source().parameterBlock().descriptor.set;
    input.bindingLayouts = {pass.bindingLayouts.front()->description(), material.layout()->description()};
    input.colorAttachmentCount = static_cast<uint32_t>(pass.attachments.colors.size());
    for (const auto& shader : material.program()->shaders())
        if (shader->source().stage() == asset::ShaderStage::Fragment)
            input.fragmentInterface = &shader->source().interface();
    const auto policy = MaterialPipelineCompiler{}.compile(input, key);
    auto result = pass;
    result.shaders.clear();
    for (const auto& shader : material.program()->shaders()) result.shaders.push_back(shader->resource());
    result.bindingLayouts = {pass.bindingLayouts.front(), material.layout()};
    result.vertexLayout = policy.vertexLayout;
    result.constantRanges = policy.constantRanges;
    result.specializations = policy.specializations;
    if (policy.cullMode) result.rasterization.cullMode = *policy.cullMode;
    if (policy.depthTestEnabled) result.depthStencil.depthTest = *policy.depthTestEnabled;
    if (policy.depthWriteEnabled) result.depthStencil.depthWrite = *policy.depthWriteEnabled;
    if (policy.depthCompare) result.depthStencil.depthCompare = *policy.depthCompare;
    if (policy.colorBlend)
        for (auto& color : result.colorBlend.attachments) color.blend = *policy.colorBlend;
    return result;
}

PreparedDrawList DrawCompiler::compile(const RenderList& source, const RenderAssetStore& resources,
    const rhi::GraphicsPipelineDesc& pass, PipelineLibrary& pipelines) const
{
    validatePass(pass);
    Resolved resolved;
    PreparedDrawList result;
    result.opaque = compileItems(source.opaque, source.objectData.size(), false, resources, pass, pipelines, resolved);
    result.transparent = compileItems(source.transparent, source.objectData.size(), true, resources, pass, pipelines, resolved);
    return result;
}
} // namespace rubia::render
