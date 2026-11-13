#pragma once
namespace rubia::render
{
enum class RenderResult
{
    Rendered,
    Skipped,
    NeedsResize,
    // GPU submission succeeded even though presentation needs reconfiguration.
    RenderedNeedsResize
};
constexpr bool wasSubmitted(RenderResult result) noexcept
{ return result == RenderResult::Rendered || result == RenderResult::RenderedNeedsResize; }
constexpr bool needsResize(RenderResult result) noexcept
{ return result == RenderResult::NeedsResize || result == RenderResult::RenderedNeedsResize; }
}
