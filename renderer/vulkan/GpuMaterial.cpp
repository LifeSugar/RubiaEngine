#include "vulkan/GpuMaterial.hpp"

#include "asset/MaterialTemplateAsset.hpp"
#include "vulkan/Device.hpp"
#include "vulkan/GpuTexture.hpp"

#include <cstring>
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
    VkDescriptorSet descriptorSet)
{
    if (!device || !asset || asset.parameterData().empty() ||
        descriptorSet == VK_NULL_HANDLE)
    {
        throw std::invalid_argument(
            "cannot create GpuMaterial from incomplete inputs");
    }
    validateTextures(textures);

    Buffer parameterBuffer(
        device,
        asset.parameterData().size(),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    void* destination = parameterBuffer.map();
    std::memcpy(
        destination,
        asset.parameterData().data(),
        asset.parameterData().size());
    parameterBuffer.unmap();

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = parameterBuffer.get();
    bufferInfo.range = asset.parameterData().size();

    VkWriteDescriptorSet parameterWrite{};

    parameterWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    parameterWrite.dstSet = descriptorSet;
    parameterWrite.dstBinding =
        materialTemplate.parameterBlock().descriptor.binding;
    parameterWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    parameterWrite.descriptorCount = 1;
    parameterWrite.pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(
        device.get(),
        1,
        &parameterWrite,
        0,
        nullptr);
    writeTextureDescriptors(
        device.get(),
        descriptorSet,
        materialTemplate,
        textures);

    reset();
    parameterBuffer_ = std::move(parameterBuffer);
    materialTemplate_ = asset.materialTemplate();
    renderState_ = asset.renderState();
    descriptorSet_ = descriptorSet;
}

void GpuMaterial::updateTextures(
    const Device& device,
    const asset::MaterialTemplateAsset& materialTemplate,
    const std::vector<const GpuTexture*>& textures)
{
    if (!device || !*this)
    {
        throw std::invalid_argument(
            "cannot update textures on an incompatible GpuMaterial");
    }
    writeTextureDescriptors(
        device.get(),
        descriptorSet_,
        materialTemplate,
        textures);
}

void GpuMaterial::reset() noexcept
{
    descriptorSet_ = VK_NULL_HANDLE;
    materialTemplate_ = {};
    renderState_ = {};
    parameterBuffer_.reset();
}

} // namespace rubia::rhi::vulkan
