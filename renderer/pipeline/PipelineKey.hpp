#pragma once
#include "rhi/GraphicsPipeline.hpp"
#include <cstddef>
#include <vector>

namespace rubia::render
{
/// Complete content key for the currently supported graphics-pipeline state.
/// Equality checks the field encoding, never just a digest. Device is cache scope.
class PipelineKey final
{
  public:
    static PipelineKey from(const rhi::GraphicsPipelineDesc &info);
    size_t hash() const noexcept
    {
        return hash_;
    }
    bool operator==(const PipelineKey &other) const noexcept
    {
        return words_ == other.words_;
    }
    bool operator!=(const PipelineKey &other) const noexcept
    {
        return !(*this == other);
    }

  private:
    std::vector<uint32_t> words_;
    size_t hash_ = 0;
};
struct PipelineKeyHash
{
    size_t operator()(const PipelineKey &key) const noexcept
    {
        return key.hash();
    }
};
} // namespace rubia::render
