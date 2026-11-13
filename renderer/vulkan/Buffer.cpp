#include "vulkan/Buffer.hpp"

#include "vulkan/Device.hpp"

#include <stdexcept>
#include <cstring>
#include <cstddef>
#include <limits>
#include <utility>

namespace rubia::rhi::vulkan
{

#if !VK_RENDERER_USE_VMA
namespace
{

uint32_t findMemoryType(
    VkPhysicalDevice physicalDevice,
    uint32_t typeFilter,
    VkMemoryPropertyFlags requiredProperties)
{
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);

    for (uint32_t index = 0; index < memoryProperties.memoryTypeCount; ++index)
    {
        const bool isCompatible = (typeFilter & (1u << index)) != 0;
        const bool hasRequiredProperties =
            (memoryProperties.memoryTypes[index].propertyFlags &
                requiredProperties) == requiredProperties;
        if (isCompatible && hasRequiredProperties)
        {
            return index;
        }
    }

    throw std::runtime_error("failed to find a suitable buffer memory type!");
}

} // namespace
#endif

Buffer::Buffer(
    const Device& device,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags memoryProperties)
{
    create(device, size, usage, memoryProperties);
}

Buffer::~Buffer()
{
    reset();
}

Buffer::Buffer(Buffer&& other) noexcept
#if VK_RENDERER_USE_VMA
    : allocator_(std::exchange(other.allocator_, VK_NULL_HANDLE)),
#else
    : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
#endif
      buffer_(std::exchange(other.buffer_, VK_NULL_HANDLE)),
#if VK_RENDERER_USE_VMA
      allocation_(std::exchange(other.allocation_, VK_NULL_HANDLE)),
#else
      memory_(std::exchange(other.memory_, VK_NULL_HANDLE)),
#endif
      size_(std::exchange(other.size_, 0)),
      mappedData_(std::exchange(other.mappedData_, nullptr))
{
    ownerDevice_ = std::exchange(other.ownerDevice_, VK_NULL_HANDLE);
    usage_ = std::exchange(other.usage_, 0);
    memoryProperties_ = std::exchange(other.memoryProperties_, 0);
}

Buffer& Buffer::operator=(Buffer&& other) noexcept
{
    if (this != &other)
    {
        reset();
        ownerDevice_ = std::exchange(other.ownerDevice_, VK_NULL_HANDLE);
        usage_ = std::exchange(other.usage_, 0);
        memoryProperties_ = std::exchange(other.memoryProperties_, 0);
#if VK_RENDERER_USE_VMA
        allocator_ = std::exchange(other.allocator_, VK_NULL_HANDLE);
#else
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
#endif
        buffer_ = std::exchange(other.buffer_, VK_NULL_HANDLE);
#if VK_RENDERER_USE_VMA
        allocation_ = std::exchange(other.allocation_, VK_NULL_HANDLE);
#else
        memory_ = std::exchange(other.memory_, VK_NULL_HANDLE);
#endif
        size_ = std::exchange(other.size_, 0);
        mappedData_ = std::exchange(other.mappedData_, nullptr);
    }
    return *this;
}

void Buffer::create(
    const Device& device,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags memoryProperties)
{
    if (!device || size == 0)
    {
        throw std::invalid_argument("cannot create a Buffer with an invalid device or zero size");
    }

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkMemoryPropertyFlags selectedProperties = 0;

#if VK_RENDERER_USE_VMA
    VmaAllocationCreateInfo allocationCreateInfo{};
    allocationCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocationCreateInfo.requiredFlags = memoryProperties;
    if ((memoryProperties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0)
    {
        allocationCreateInfo.flags |=
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    }

    VkBuffer newBuffer = VK_NULL_HANDLE;
    VmaAllocation newAllocation = VK_NULL_HANDLE;
    if (vmaCreateBuffer(
            device.allocator(),
            &bufferInfo,
            &allocationCreateInfo,
            &newBuffer,
            &newAllocation,
            nullptr) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to create VMA buffer allocation!");
    }

    reset();
    allocator_ = device.allocator();
    buffer_ = newBuffer;
    allocation_ = newAllocation;
    vmaGetAllocationMemoryProperties(allocator_, allocation_, &selectedProperties);
#else
    VkBuffer newBuffer = VK_NULL_HANDLE;
    VkDeviceMemory newMemory = VK_NULL_HANDLE;
    if (vkCreateBuffer(
            device.get(),
            &bufferInfo,
            nullptr,
            &newBuffer) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to create buffer!");
    }

    try
    {
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(
            device.get(),
            newBuffer,
            &requirements);

        VkMemoryAllocateInfo allocationInfo{};
        allocationInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocationInfo.allocationSize = requirements.size;
        allocationInfo.memoryTypeIndex = findMemoryType(
            device.physical(),
            requirements.memoryTypeBits,
            memoryProperties);
        VkPhysicalDeviceMemoryProperties physicalMemory{};
        vkGetPhysicalDeviceMemoryProperties(device.physical(), &physicalMemory);
        selectedProperties = physicalMemory.memoryTypes[allocationInfo.memoryTypeIndex].propertyFlags;

        if (vkAllocateMemory(
                device.get(),
                &allocationInfo,
                nullptr,
                &newMemory) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to allocate buffer memory!");
        }
        if (vkBindBufferMemory(
                device.get(),
                newBuffer,
                newMemory,
                0) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to bind buffer memory!");
        }
    }
    catch (...)
    {
        vkDestroyBuffer(device.get(), newBuffer, nullptr);
        if (newMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(device.get(), newMemory, nullptr);
        }
        throw;
    }

    reset();
    device_ = device.get();
    buffer_ = newBuffer;
    memory_ = newMemory;
#endif
    size_ = size;
    ownerDevice_ = device.get();
    usage_ = usage;
    memoryProperties_ = selectedProperties;
}

void Buffer::reset() noexcept
{
    ownerDevice_ = VK_NULL_HANDLE;
    usage_ = 0;
    memoryProperties_ = 0;
    unmap();

#if VK_RENDERER_USE_VMA
    if (allocator_ != VK_NULL_HANDLE && buffer_ != VK_NULL_HANDLE)
    {
        vmaDestroyBuffer(allocator_, buffer_, allocation_);
    }

    allocator_ = VK_NULL_HANDLE;
    buffer_ = VK_NULL_HANDLE;
    allocation_ = VK_NULL_HANDLE;
#else
    if (device_ != VK_NULL_HANDLE && buffer_ != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(device_, buffer_, nullptr);
    }
    if (device_ != VK_NULL_HANDLE && memory_ != VK_NULL_HANDLE)
    {
        vkFreeMemory(device_, memory_, nullptr);
    }

    device_ = VK_NULL_HANDLE;
    buffer_ = VK_NULL_HANDLE;
    memory_ = VK_NULL_HANDLE;
#endif
    size_ = 0;
}

void* Buffer::map(VkDeviceSize offset, VkDeviceSize size)
{
#if VK_RENDERER_USE_VMA
    const bool isEmpty = allocation_ == VK_NULL_HANDLE;
#else
    const bool isEmpty = memory_ == VK_NULL_HANDLE;
#endif
    if (isEmpty)
    {
        throw std::logic_error("cannot map an empty Buffer");
    }
    if (mappedData_ != nullptr)
    {
        throw std::logic_error("Buffer memory is already mapped");
    }

    if (offset >= size_ || size == 0 ||
        (size != VK_WHOLE_SIZE && size > size_ - offset))
    {
        throw std::out_of_range("buffer mapping range is out of bounds");
    }

#if VK_RENDERER_USE_VMA
    if (vmaMapMemory(allocator_, allocation_, &mappedData_) != VK_SUCCESS)
    {
        mappedData_ = nullptr;
        throw std::runtime_error("failed to map buffer memory!");
    }
    return static_cast<char*>(mappedData_) + offset;
#else
    if (vkMapMemory(
            device_,
            memory_,
            offset,
            size,
            0,
            &mappedData_) != VK_SUCCESS)
    {
        mappedData_ = nullptr;
        throw std::runtime_error("failed to map buffer memory!");
    }
    return mappedData_;
#endif
}

void Buffer::unmap() noexcept
{
#if VK_RENDERER_USE_VMA
    if (allocator_ != VK_NULL_HANDLE && allocation_ != VK_NULL_HANDLE && mappedData_ != nullptr)
    {
        vmaUnmapMemory(allocator_, allocation_);
        mappedData_ = nullptr;
    }
#else
    if (device_ != VK_NULL_HANDLE && memory_ != VK_NULL_HANDLE &&
        mappedData_ != nullptr)
    {
        vkUnmapMemory(device_, memory_);
        mappedData_ = nullptr;
    }
#endif
}

void Buffer::write(const void* data, VkDeviceSize size, VkDeviceSize offset)
{
    if (!data || !size || offset > size_ || size > size_ - offset ||
        size > std::numeric_limits<std::size_t>::max() ||
        offset > std::numeric_limits<std::size_t>::max() ||
        !(memoryProperties_ & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
        throw std::invalid_argument("buffer write requires valid bytes and host-visible memory");
    auto* destination = static_cast<std::byte*>(map());
    std::memcpy(destination + static_cast<std::size_t>(offset), data, static_cast<std::size_t>(size));
    VkResult result = VK_SUCCESS;
    if (!(memoryProperties_ & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
    {
#if VK_RENDERER_USE_VMA
        // VMA aligns the range to nonCoherentAtomSize within its mapped block.
        result = vmaFlushAllocation(allocator_, allocation_, offset, size);
#else
        // This backend owns a dedicated allocation and map() maps all of it.
        // Flushing the whole allocation satisfies atom alignment at both ends.
        VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
        range.memory = memory_;
        range.offset = 0;
        range.size = VK_WHOLE_SIZE;
        result = vkFlushMappedMemoryRanges(device_, 1, &range);
#endif
    }
    unmap();
    if (result != VK_SUCCESS)
        throw std::runtime_error("failed to flush buffer writes");
}

VkDeviceMemory Buffer::memory() const noexcept
{
#if VK_RENDERER_USE_VMA
    if (allocator_ == VK_NULL_HANDLE || allocation_ == VK_NULL_HANDLE)
    {
        return VK_NULL_HANDLE;
    }
    VmaAllocationInfo allocationInfo{};
    vmaGetAllocationInfo(allocator_, allocation_, &allocationInfo);
    return allocationInfo.deviceMemory;
#else
    return memory_;
#endif
}

} // namespace rubia::rhi::vulkan
