#pragma once
#include "core/PreparedDrawList.hpp"
#include "rhi/RenderEncoder.hpp"
namespace rubia::render
{
void recordDrawList(rhi::RenderEncoder&, const rhi::BindingSetRef& frameBindings,
                    uint32_t width, uint32_t height, const PreparedDrawList&);
}
