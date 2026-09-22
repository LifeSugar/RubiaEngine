#include "vulkan/UploadValidation.hpp"
#include <algorithm>
#include <limits>

namespace rubia::rhi::vulkan
{
namespace
{
bool rangeFits(uint32_t base, uint32_t count, uint32_t available)
{
    return count && base <= available && count <= available - base;
}
void validateSource(const UploadBytes& source)
{
    if (!source.data || !source.size)
    {
        throw std::invalid_argument("upload requires nonempty source bytes");
    }
}
uint64_t add(uint64_t a, uint64_t b)
{
    if (b > std::numeric_limits<uint64_t>::max() - a)
    {
        throw std::invalid_argument("image source span overflows");
    }
    return a + b;
}
uint64_t multiply(uint64_t a, uint64_t b)
{
    if (b && a > std::numeric_limits<uint64_t>::max() / b)
    {
        throw std::invalid_argument("image source span overflows");
    }
    return a * b;
}
uint64_t blocks(uint32_t texels, uint32_t blockSize)
{
    return (uint64_t(texels) + blockSize - 1) / blockSize;
}
struct CopyFormat
{
    uint32_t width, height, bytes;
};
// Vulkan texel-block metadata. No dependency on the asset format vocabulary.
CopyFormat copyFormat(VkFormat format)
{
    switch (format)
    {
    case VK_FORMAT_R8_UNORM:
    case VK_FORMAT_R8_SNORM:
    case VK_FORMAT_R8_UINT:
    case VK_FORMAT_R8_SINT:
    case VK_FORMAT_R8_SRGB:
        return {1, 1, 1};
    case VK_FORMAT_R8G8_UNORM:
    case VK_FORMAT_R8G8_SNORM:
    case VK_FORMAT_R8G8_UINT:
    case VK_FORMAT_R8G8_SINT:
    case VK_FORMAT_R8G8_SRGB:
    case VK_FORMAT_R16_SFLOAT:
    case VK_FORMAT_R16_UNORM:
        return {1, 1, 2};
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_R8G8B8A8_SNORM:
    case VK_FORMAT_R8G8B8A8_UINT:
    case VK_FORMAT_R8G8B8A8_SINT:
    case VK_FORMAT_R8G8B8A8_SRGB:
    case VK_FORMAT_B8G8R8A8_UNORM:
    case VK_FORMAT_B8G8R8A8_SRGB:
    case VK_FORMAT_R16G16_SFLOAT:
    case VK_FORMAT_R32_SFLOAT:
    case VK_FORMAT_R32_UINT:
    case VK_FORMAT_R32_SINT:
        return {1, 1, 4};
    case VK_FORMAT_R16G16B16A16_SFLOAT:
    case VK_FORMAT_R32G32_SFLOAT:
        return {1, 1, 8};
    case VK_FORMAT_R32G32B32A32_SFLOAT:
        return {1, 1, 16};
    case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
    case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
    case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
    case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
    case VK_FORMAT_BC4_UNORM_BLOCK:
    case VK_FORMAT_BC4_SNORM_BLOCK:
        return {4, 4, 8};
    case VK_FORMAT_BC2_UNORM_BLOCK:
    case VK_FORMAT_BC2_SRGB_BLOCK:
    case VK_FORMAT_BC3_UNORM_BLOCK:
    case VK_FORMAT_BC3_SRGB_BLOCK:
    case VK_FORMAT_BC5_UNORM_BLOCK:
    case VK_FORMAT_BC5_SNORM_BLOCK:
    case VK_FORMAT_BC6H_UFLOAT_BLOCK:
    case VK_FORMAT_BC6H_SFLOAT_BLOCK:
    case VK_FORMAT_BC7_UNORM_BLOCK:
    case VK_FORMAT_BC7_SRGB_BLOCK:
        return {4, 4, 16};
    default:
        throw UnsupportedUpload("image copy format is not supported by this upload service");
    }
}
void validateState(const ImageAccessState& state, VkImageUsageFlags usage, bool before)
{
    if (!state.stages)
    {
        throw std::invalid_argument("image upload requires explicit before/after stages");
    }
    VkImageUsageFlags required = 0;
    switch (state.layout)
    {
    case VK_IMAGE_LAYOUT_UNDEFINED:
        if (!before || state.access)
        {
            throw std::invalid_argument(
                "UNDEFINED is only valid as a discard-before state with no access");
        }
        break;
    case VK_IMAGE_LAYOUT_GENERAL:
        break;
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        required = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        break;
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        required = VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        break;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        required = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
        break;
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        required = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        break;
    default:
        throw UnsupportedUpload("image boundary layout is not supported by this upload service");
    }
    if (required && !(usage & required))
    {
        throw std::invalid_argument("image boundary layout is incompatible with image usage");
    }
    constexpr auto shaders =
        VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT |
        VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT |
        VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    VkPipelineStageFlags stages = state.stages;
    if (stages & VK_PIPELINE_STAGE_ALL_COMMANDS_BIT)
    {
        stages = ~VkPipelineStageFlags{0};
    }
    if (stages & VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT)
    {
        stages |= (shaders & ~VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT) |
                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    }
    const auto check = [&](VkAccessFlags accesses, VkPipelineStageFlags supported)
    {
        if ((state.access & accesses) && !(stages & supported))
        {
            throw std::invalid_argument("image access mask is incompatible with stage mask");
        }
    };
    check(VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
          VK_PIPELINE_STAGE_TRANSFER_BIT);
    check(VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, shaders);
    check(VK_ACCESS_INPUT_ATTACHMENT_READ_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    check(VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    check(VK_ACCESS_HOST_READ_BIT | VK_ACCESS_HOST_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT);
    constexpr VkAccessFlags supportedAccess =
        VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT |
        VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_INPUT_ATTACHMENT_READ_BIT |
        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_HOST_READ_BIT | VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT |
        VK_ACCESS_MEMORY_WRITE_BIT;
    if (state.access & ~supportedAccess)
    {
        throw UnsupportedUpload("image access mask is outside the supported color-image subset");
    }
}
bool overlaps(uint64_t a, uint64_t an, uint64_t b, uint64_t bn)
{
    return a < b + bn && b < a + an;
}
} // namespace

void validateBufferUpload(const BufferUpload& op)
{
    validateSource(op.source);
    if (!op.destination || !*op.destination || op.destinationOffset > op.destination->size() ||
        op.source.size > op.destination->size() - op.destinationOffset ||
        op.destinationOffset % 4 || op.source.size % 4 || !op.finalStages || !op.finalAccess ||
        !(op.destination->usage() & VK_BUFFER_USAGE_TRANSFER_DST_BIT))
    {
        throw std::invalid_argument("invalid buffer upload range, usage or synchronization");
    }
}

//为什么不行
void validateImageUpload(const ImageUpload& op)
{
    //没destination
    validateSource(op.source);
    if (!op.destination || !*op.destination)
    {
        throw std::invalid_argument("image upload requires an existing destination");
    }
    const auto& d = op.destination->description();
    //现在只支持2D color，之后的之后再说
    if (d.type != VK_IMAGE_TYPE_2D || op.range.aspectMask != VK_IMAGE_ASPECT_COLOR_BIT)
    {
        throw UnsupportedUpload("upload service supports 2D color images only");
    }
    //目标不符合上传操作的规格
    if (d.samples != VK_SAMPLE_COUNT_1_BIT || !(d.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) ||
        !rangeFits(op.range.baseMipLevel, op.range.levelCount, d.mipLevels) ||
        !rangeFits(op.range.baseArrayLayer, op.range.layerCount, d.arrayLayers))
    {
        throw std::invalid_argument("invalid image upload range, sample count or usage");
    }
    validateState(op.before, d.usage, true);
    validateState(op.after, d.usage, false);
    if (op.regions.empty() || op.regions.size() > std::numeric_limits<uint32_t>::max())
    {
        throw std::invalid_argument("invalid image upload region count");
    }
    const auto format = copyFormat(d.format);
    for (size_t index = 0; index < op.regions.size(); ++index)
    {
        const auto& r = op.regions[index];
        const auto& sub = r.imageSubresource;
        if (sub.aspectMask != op.range.aspectMask || sub.mipLevel < op.range.baseMipLevel ||
            sub.mipLevel - op.range.baseMipLevel >= op.range.levelCount ||
            sub.baseArrayLayer < op.range.baseArrayLayer ||
            !rangeFits(sub.baseArrayLayer - op.range.baseArrayLayer, sub.layerCount,
                       op.range.layerCount))
        {
            throw std::invalid_argument("image copy is outside the upload subresource range");
        }
        const auto width = std::max(1u, d.extent.width >> std::min(sub.mipLevel, 31u));
        const auto height = std::max(1u, d.extent.height >> std::min(sub.mipLevel, 31u));
        if (r.imageOffset.x < 0 || r.imageOffset.y < 0 || r.imageOffset.z ||
            r.imageExtent.depth != 1 ||
            !rangeFits(uint32_t(r.imageOffset.x), r.imageExtent.width, width) ||
            !rangeFits(uint32_t(r.imageOffset.y), r.imageExtent.height, height))
        {
            throw std::invalid_argument("invalid image copy offset or extent");
        }
        if ((r.bufferRowLength && r.bufferRowLength < r.imageExtent.width) ||
            (r.bufferImageHeight && r.bufferImageHeight < r.imageExtent.height) ||
            r.bufferRowLength % format.width || r.bufferImageHeight % format.height ||
            uint32_t(r.imageOffset.x) % format.width || uint32_t(r.imageOffset.y) % format.height ||
            (r.imageExtent.width % format.width &&
             uint64_t(r.imageOffset.x) + r.imageExtent.width != width) ||
            (r.imageExtent.height % format.height &&
             uint64_t(r.imageOffset.y) + r.imageExtent.height != height) ||
            r.bufferOffset % format.bytes)
        {
            throw std::invalid_argument("invalid image copy pitch or texel-block alignment");
        }
        const auto row =
            blocks(r.bufferRowLength ? r.bufferRowLength : r.imageExtent.width, format.width);
        const auto rows =
            blocks(r.bufferImageHeight ? r.bufferImageHeight : r.imageExtent.height, format.height);
        if (multiply(row, format.bytes) > uint64_t(std::numeric_limits<int32_t>::max()))
        {
            throw std::invalid_argument("image copy row stride exceeds Vulkan limit");
        }
        // Padding between rows/layers is included; trailing padding after the last row is not read.
        const auto precedingLayers = multiply(sub.layerCount - 1, multiply(row, rows));
        const auto precedingRows = multiply(blocks(r.imageExtent.height, format.height) - 1, row);
        const auto span = multiply(
            add(add(precedingLayers, precedingRows), blocks(r.imageExtent.width, format.width)),
            format.bytes);
        if (add(r.bufferOffset, span) > op.source.size)
        {
            throw std::invalid_argument("image copy exceeds source bytes");
        }
        for (size_t previous = 0; previous < index; ++previous)
        {
            const auto& p = op.regions[previous];
            if (p.imageSubresource.mipLevel == sub.mipLevel &&
                overlaps(p.imageSubresource.baseArrayLayer, p.imageSubresource.layerCount,
                         sub.baseArrayLayer, sub.layerCount) &&
                overlaps(p.imageOffset.x, p.imageExtent.width, r.imageOffset.x,
                         r.imageExtent.width) &&
                overlaps(p.imageOffset.y, p.imageExtent.height, r.imageOffset.y,
                         r.imageExtent.height))
            {
                throw std::invalid_argument("image copy destination regions overlap");
            }
        }
    }
}
} // namespace rubia::rhi::vulkan
