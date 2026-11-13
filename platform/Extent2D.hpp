#pragma once

#include <cstdint>

namespace rubia::platform
{

/// Drawable dimensions in physical pixels, independent of the graphics API.
struct Extent2D
{
    uint32_t width = 0;
    uint32_t height = 0;
};

} // namespace rubia::platform
