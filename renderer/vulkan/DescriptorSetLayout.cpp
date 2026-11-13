#include "vulkan/DescriptorSetLayout.hpp"
#include "vulkan/PipelineKeyData.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace rubia::rhi::vulkan
{
DescriptorSetLayout::DescriptorSetLayout(VkDevice device,
                                         const std::vector<VkDescriptorSetLayoutBinding> &bindings,
                                         VkDescriptorSetLayoutCreateFlags flags)
{
    create(device, bindings, flags);
}
DescriptorSetLayout::~DescriptorSetLayout() = default;
DescriptorSetLayout::DescriptorSetLayout(DescriptorSetLayout &&other) noexcept = default;
DescriptorSetLayout &DescriptorSetLayout::operator=(DescriptorSetLayout &&other) noexcept = default;
void DescriptorSetLayout::reset() noexcept
{
    state_.reset();
}
DescriptorSetLayoutState::~DescriptorSetLayoutState()
{
    if (handle_)
        vkDestroyDescriptorSetLayout(device_, handle_, nullptr);
}

void DescriptorSetLayout::create(VkDevice device,
                                 const std::vector<VkDescriptorSetLayoutBinding> &bindings,
                                 VkDescriptorSetLayoutCreateFlags flags)
{
    if (!device)
        throw std::invalid_argument("descriptor layout requires a device");
    auto sorted = bindings;
    std::sort(sorted.begin(), sorted.end(),
              [](const auto &a, const auto &b) { return a.binding < b.binding; });
    detail::PipelineKeyData key;
    key.add(flags);
    key.count(sorted.size());
    for (size_t i = 0; i < sorted.size(); ++i)
    {
        const auto &b = sorted[i];
        if (i && sorted[i - 1].binding == b.binding)
            throw std::invalid_argument("duplicate descriptor layout binding");
        // Supporting these requires owned sampler descriptions, not recyclable
        // sampler handles. Existing layouts have no immutable samplers.
        if (b.pImmutableSamplers)
            throw std::invalid_argument("immutable samplers are not supported by layout snapshots");
        key.add(b.binding);
        key.add(b.descriptorType);
        key.add(b.descriptorCount);
        key.add(b.stageFlags);
    }
    auto state = std::shared_ptr<DescriptorSetLayoutState>(new DescriptorSetLayoutState);
    state->device_ = device;
    state->key_ = std::move(key.words);
    state->bindings_ = std::move(sorted);
    VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    info.flags = flags;
    info.bindingCount = static_cast<uint32_t>(bindings.size());
    info.pBindings = bindings.data();
    if (vkCreateDescriptorSetLayout(device, &info, nullptr, &state->handle_) != VK_SUCCESS)
        throw std::runtime_error("failed to create descriptor set layout");
    state_ = std::move(state);
}
} // namespace rubia::rhi::vulkan
