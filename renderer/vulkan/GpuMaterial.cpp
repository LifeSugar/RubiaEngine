#include "vulkan/GpuMaterial.hpp"

#include "asset/MaterialTemplateAsset.hpp"
#include "vulkan/Device.hpp"
#include "vulkan/GpuTexture.hpp"

#include <stdexcept>
#include <utility>
#include <vector>

namespace rubia::rhi::vulkan
{
namespace
{

void validateTextures(const std::vector<const GpuTexture*>& textures)
{
    for (const GpuTexture* texture : textures)
    {
        if (texture == nullptr || !*texture)
        {
            throw std::invalid_argument(
                "GpuMaterial references an invalid GpuTexture");
        }
    }
}

void writeParameterDescriptor(VkDevice device, VkDescriptorSet set,
                              const asset::MaterialTemplateAsset& materialTemplate,
                              const Buffer& buffer)
{
    VkDescriptorBufferInfo info{};
    info.buffer = buffer.get();
    info.range = buffer.size();
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = set;
    write.dstBinding = materialTemplate.parameterBlock().descriptor.binding;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.descriptorCount = 1;
    write.pBufferInfo = &info;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

void writeTextureDescriptors(
    VkDevice device,
    VkDescriptorSet descriptorSet,
    const asset::MaterialTemplateAsset& materialTemplate,
    const std::vector<const GpuTexture*>& textures)
{
    validateTextures(textures);
    if (device == VK_NULL_HANDLE || descriptorSet == VK_NULL_HANDLE)
    {
        throw std::invalid_argument(
            "GpuMaterial texture descriptor destination is invalid");
    }

    const std::vector<asset::MaterialTextureSlotDesc>& textureSlots =
        materialTemplate.textureSlots();
    const uint32_t textureSlotCount =
        static_cast<uint32_t>(textureSlots.size());
    for (const asset::MaterialTextureSlotDesc& slot : textureSlots)
    {
        if (slot.slot >= textures.size())
        {
            throw std::invalid_argument(
                "GpuMaterial texture slot is outside its compiled texture table");
        }

    }

    // Validation above is complete before the first descriptor is changed.
    // Per-slot stack storage keeps this commit path allocation-free.
    for (uint32_t index = 0; index < textureSlotCount; ++index)
    {
        const asset::MaterialTextureSlotDesc& slot = textureSlots[index];
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageView = textures[slot.slot]->view();
        imageInfo.imageLayout =
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkDescriptorImageInfo samplerInfo{};
        samplerInfo.sampler = textures[slot.slot]->sampler();

        VkWriteDescriptorSet writes[2]{};
        VkWriteDescriptorSet& imageWrite = writes[0];
        imageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        imageWrite.dstSet = descriptorSet;
        imageWrite.dstBinding = slot.imageBinding.binding;
        imageWrite.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        imageWrite.descriptorCount = 1;
        imageWrite.pImageInfo = &imageInfo;

        VkWriteDescriptorSet& samplerWrite = writes[1];
        samplerWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        samplerWrite.dstSet = descriptorSet;
        samplerWrite.dstBinding = slot.samplerBinding.binding;
        samplerWrite.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        samplerWrite.descriptorCount = 1;
        samplerWrite.pImageInfo = &samplerInfo;

        vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
    }
}

} // namespace

void GpuMaterial::create(
    const Device& device,
    const asset::MaterialAsset& asset,
    const asset::MaterialTemplateAsset& materialTemplate,
    const std::vector<const GpuTexture*>& textures,
    VkDescriptorSet descriptorSet,
    std::shared_ptr<const Buffer> parameterBuffer)
{
    if (!device || !asset || asset.parameterData().empty() ||
        descriptorSet == VK_NULL_HANDLE || !parameterBuffer || !*parameterBuffer ||
        parameterBuffer->ownerDevice() != device.get() ||
        parameterBuffer->size() != asset.parameterData().size() ||
        !(parameterBuffer->usage() & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT))
    {
        throw std::invalid_argument(
            "cannot create GpuMaterial from incomplete inputs");
    }
    validateTextures(textures);

    writeTextureDescriptors(
        device.get(),
        descriptorSet,
        materialTemplate,
        textures);

    writeParameterDescriptor(device.get(), descriptorSet, materialTemplate, *parameterBuffer);

    reset();
    parameterBuffer_ = std::move(parameterBuffer);
    materialTemplate_ = asset.materialTemplate();
    renderState_ = asset.renderState();
    descriptorSet_ = descriptorSet;
}

GpuMaterial GpuMaterial::withTextures(
    const Device& device,
    const asset::MaterialTemplateAsset& materialTemplate,
    const std::vector<const GpuTexture*>& textures,
    VkDescriptorSet descriptorSet) const
{
    if (!device || !*this || descriptorSet == VK_NULL_HANDLE || descriptorSet == descriptorSet_)
    {
        throw std::invalid_argument(
            "texture rebinding requires a valid material and a new descriptor set");
    }
    writeTextureDescriptors(device.get(), descriptorSet, materialTemplate, textures);
    writeParameterDescriptor(device.get(), descriptorSet, materialTemplate, *parameterBuffer_);
    GpuMaterial result;
    result.parameterBuffer_ = parameterBuffer_;
    result.materialTemplate_ = materialTemplate_;
    result.renderState_ = renderState_;
    result.descriptorSet_ = descriptorSet;
    return result;
}

GpuMaterial GpuMaterial::fromParameters(const Device& device,
    const asset::MaterialTemplateAsset& layout, const std::vector<const GpuTexture*>& textures,
    VkDescriptorSet set, const ParameterSnapshot& source)
{
    if (!device || !set || !source.buffer || !*source.buffer || !source.materialTemplate ||
        source.buffer->ownerDevice() != device.get())
        throw std::invalid_argument("invalid material parameter snapshot");
    writeTextureDescriptors(device.get(), set, layout, textures);
    writeParameterDescriptor(device.get(), set, layout, *source.buffer);
    GpuMaterial result;
    result.parameterBuffer_ = source.buffer;
    result.materialTemplate_ = source.materialTemplate;
    result.renderState_ = source.renderState;
    result.descriptorSet_ = set;
    return result;
}

void GpuMaterial::reset() noexcept
{
    descriptorSet_ = VK_NULL_HANDLE;
    materialTemplate_ = {};
    renderState_ = {};
    parameterBuffer_.reset();
}

} // namespace rubia::rhi::vulkan
