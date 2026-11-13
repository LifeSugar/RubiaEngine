#pragma once
#include "asset/MaterialAsset.hpp"
#include "resources/TextureResource.hpp"
#include <vector>
namespace rubia::asset
{
class MaterialTemplateAsset;
}
namespace rubia::render
{
/// Published parameter bytes and descriptors are immutable. Texture replacement
/// builds a new BindingSet and retains the exact existing parameter allocation.
class MaterialBinding final
{
  public:
    MaterialBinding() = default;
    MaterialBinding(const MaterialBinding &) = delete;
    MaterialBinding &operator=(const MaterialBinding &) = delete;
    MaterialBinding(MaterialBinding &&) noexcept = default;
    MaterialBinding &operator=(MaterialBinding &&) noexcept = default;
    struct ParameterSnapshot
    {
        rhi::BufferRef buffer;
        asset::MaterialTemplateAssetHandle materialTemplate;
        asset::MaterialRenderState renderState;
    };
    void create(const rhi::Device &, const asset::MaterialAsset &,
                const asset::MaterialTemplateAsset &, const std::vector<const TextureResource *> &,
                rhi::BindingLayoutRef, rhi::BufferRef);
    MaterialBinding withTextures(const rhi::Device &, const asset::MaterialTemplateAsset &,
                                 const std::vector<const TextureResource *> &,
                                 rhi::BindingLayoutRef) const;
    static MaterialBinding fromParameters(const rhi::Device &, const asset::MaterialTemplateAsset &,
                                          const std::vector<const TextureResource *> &,
                                          rhi::BindingLayoutRef, const ParameterSnapshot &);
    ParameterSnapshot parameterSnapshot() const
    {
        return {parameterBuffer_, materialTemplate_, renderState_};
    }
    void reset() noexcept
    {
        bindings_.reset();
        parameterBuffer_.reset();
        materialTemplate_ = {};
        renderState_ = {};
    }
    const rhi::BindingSetRef &descriptorSet() const noexcept { return bindings_; }
    const rhi::BufferRef &parameterBuffer() const noexcept { return parameterBuffer_; }
    const rhi::BufferRef &parameterBufferResource() const noexcept { return parameterBuffer_; }
    const asset::MaterialRenderState &renderState() const noexcept { return renderState_; }
    asset::MaterialTemplateAssetHandle materialTemplate() const noexcept
    {
        return materialTemplate_;
    }
    explicit operator bool() const noexcept
    {
        return parameterBuffer_ && materialTemplate_ && bindings_;
    }

  private:
    rhi::BufferRef parameterBuffer_;
    asset::MaterialTemplateAssetHandle materialTemplate_;
    asset::MaterialRenderState renderState_;
    rhi::BindingSetRef bindings_;
};
} // namespace rubia::render
