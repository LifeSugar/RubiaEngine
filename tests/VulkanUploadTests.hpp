#pragma once
namespace rubia::rhi::vulkan
{
class Device;
}
namespace rubia::test
{
void runVulkanUploadTests(const rhi::vulkan::Device& device);
}
