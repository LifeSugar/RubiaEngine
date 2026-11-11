#pragma once
#include "rhi/Device.hpp"
namespace rubia::render
{
class OutputBindings
{
public:
    void create(const rhi::Device&, const std::vector<rhi::TextureViewRef>& sceneResolveViews);
    void rebind(const rhi::Device&, const std::vector<rhi::TextureViewRef>& sceneResolveViews);
    void reset() noexcept;
    const rhi::BindingLayoutRef& layout() const noexcept { return layout_; }
    const rhi::BindingSetRef& set(uint32_t frame) const { return sets_.at(frame); }
    size_t size() const noexcept { return sets_.size(); }
private:
    rhi::SamplerRef sampler_;
    rhi::BindingLayoutRef layout_;
    std::vector<rhi::BindingSetRef> sets_;
};
}
