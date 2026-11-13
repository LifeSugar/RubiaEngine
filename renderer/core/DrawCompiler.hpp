#pragma once
#include "core/PreparedDrawList.hpp"
#include "resources/RenderAssetStore.hpp"
#include "pipeline/PipelineLibrary.hpp"
#include "render/RenderList.hpp"

namespace rubia::render
{
class DrawCompiler final
{
public:
    static rhi::GraphicsPipelineDesc describeMaterialPipeline(const PreparedMaterialTemplate&,
        const PipelineVariantKey&, const rhi::GraphicsPipelineDesc& pass);
    PreparedDrawList compile(const RenderList&, const RenderAssetStore&,
        const rhi::GraphicsPipelineDesc& pass, PipelineLibrary&) const;
};
} // namespace rubia::render
