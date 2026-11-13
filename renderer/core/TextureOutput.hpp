#pragma once
#include "rhi/Resources.hpp"
namespace rubia::render
{
enum class ColorEncoding { Linear, DisplaySrgb };
struct TextureOutput
{
    rhi::TextureViewRef view;
    struct Extent { uint32_t width = 0, height = 0; } extent;
    uint64_t revision = 0;
    ColorEncoding colorEncoding = ColorEncoding::DisplaySrgb;
};
}
