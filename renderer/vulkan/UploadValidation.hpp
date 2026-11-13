#pragma once
#include "vulkan/VulkanUploadTypes.hpp"
#include <stdexcept>

namespace rubia::rhi::vulkan
{
class UnsupportedUpload : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};
// Structural checks only: do not infer current image layout or validate owner lifetime.
// Supported image copies: single-sampled 2D color images, including arrays and BC formats.
void validateBufferUpload(const BufferUpload& upload);
void validateImageUpload(const ImageUpload& upload);
} // namespace rubia::rhi::vulkan
