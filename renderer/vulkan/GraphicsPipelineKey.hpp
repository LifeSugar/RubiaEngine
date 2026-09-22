#pragma once
#include "vulkan/GraphicsPipeline.hpp"
#include <cstddef>
#include <vector>

namespace rubia::rhi::vulkan
{
/// Complete content key for the currently supported graphics-pipeline state.
/// Equality checks the field encoding, never just a digest. Device is cache scope.
class GraphicsPipelineKey final
{
  public:
    static GraphicsPipelineKey from(const GraphicsPipeline::CreateInfo &info);
    size_t hash() const noexcept
    {
        return hash_;
    }
    bool operator==(const GraphicsPipelineKey &other) const noexcept
    {
        return words_ == other.words_;
    }
    bool operator!=(const GraphicsPipelineKey &other) const noexcept
    {
        return !(*this == other);
    }

  private:
    std::vector<uint32_t> words_;
    size_t hash_ = 0;
};
struct GraphicsPipelineKeyHash
{
    size_t operator()(const GraphicsPipelineKey &key) const noexcept
    {
        return key.hash();
    }
};
} // namespace rubia::rhi::vulkan
