#include "core/FrameBindings.hpp"
#include "math/Frustum.hpp"
#include "render/Camera.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace rubia;
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
bool close(const glm::mat4& a, const glm::mat4& b)
{
    for (glm::length_t c = 0; c < 4; ++c)
        for (glm::length_t r = 0; r < 4; ++r)
            if (std::abs(a[c][r] - b[c][r]) > 0.0001f) return false;
    return true;
}
struct RecordingBuffer final : rhi::BufferResource
{
    rhi::BufferDesc desc;
    mutable std::vector<std::byte> bytes;
    rhi::DeviceId deviceId() const noexcept override { return 1; }
    const rhi::BufferDesc& description() const noexcept override { return desc; }
    uint32_t memoryProperties() const noexcept override { return desc.requiredMemory; }
};
struct Layout final : rhi::BindingLayoutResource
{
    rhi::BindingLayoutDesc desc;
    rhi::DeviceId deviceId() const noexcept override { return 1; }
    const rhi::BindingLayoutDesc& description() const noexcept override { return desc; }
};
struct Bindings final : rhi::BindingSetResource
{
    rhi::BindingSetDesc desc;
    rhi::DeviceId deviceId() const noexcept override { return 1; }
    const rhi::BindingSetDesc& description() const noexcept override { return desc; }
};
class RecordingDevice : public rhi::Device
{
public:
    rhi::DeviceId id() const noexcept override { return 1; }
    rhi::DeviceLimits limits() const noexcept override { return {}; }
    rhi::BufferRef createBuffer(const rhi::BufferDesc& desc) const override
    {
        auto result = std::make_shared<RecordingBuffer>();
        result->desc = desc;
        result->bytes.resize(desc.size);
        return result;
    }
    void writeBuffer(const rhi::BufferRef& buffer, const void* bytes,
                     uint64_t size, uint64_t offset) const override
    {
        const auto& target = dynamic_cast<const RecordingBuffer&>(*buffer);
        require(offset + size <= target.bytes.size(), "camera upload exceeds buffer");
        std::memcpy(target.bytes.data() + offset, bytes, size);
    }
    rhi::BindingLayoutRef createBindingLayout(const rhi::BindingLayoutDesc& desc) const override
    {
        auto result = std::make_shared<Layout>();
        result->desc = desc;
        return result;
    }
    rhi::BindingSetRef createBindingSet(const rhi::BindingSetDesc& desc) const override
    {
        auto result = std::make_shared<Bindings>();
        result->desc = desc;
        return result;
    }
    rhi::TextureRef createTexture(const rhi::TextureDesc&) const override { return {}; }
    rhi::TextureViewRef createTextureView(rhi::TextureRef, const rhi::TextureViewDesc&) const override { return {}; }
    rhi::SamplerRef createSampler(const rhi::SamplerDesc&) const override { return {}; }
    rhi::ShaderRef createShader(const rhi::ShaderCode&) const override { return {}; }
};
class CorrectedDevice final : public RecordingDevice
{
public:
    rhi::ClipSpaceConvention convention;
    rhi::ClipSpaceConvention clipSpaceConvention() const noexcept override { return convention; }
};
render::CameraGpuData uploaded(const render::FrameBindings& frames,
                              uint32_t frame, uint32_t camera)
{
    const auto& binding = std::get<rhi::BufferBinding>(
        frames.bindingSet(frame)->description().writes.at(0).resource);
    const auto& buffer = dynamic_cast<const RecordingBuffer&>(*binding.buffer);
    render::CameraGpuData data;
    std::memcpy(&data, buffer.bytes.data() + sizeof(data) * camera, sizeof(data));
    return data;
}
glm::mat4 oldVulkanViewProjection(const render::Camera& camera)
{
    const auto& config = camera.getConfig();
    auto projection = glm::perspective(glm::radians(config.fov), config.aspectRatio,
                                       config.nearPlane, config.farPlane);
    projection[1][1] *= -1.0f;
    return projection * camera.getViewMatrix();
}
void run()
{
    render::Camera first, second;
    first.setConfig({61.0f, 1.7f, 0.2f, 60.0f});
    first.setPosition({7.0f, -3.0f, 11.0f});
    first.setRotation({23.0f, -37.0f, 19.0f});
    second.setPosition({-2.0f, 4.0f, 1.0f});
    second.setRotation({-17.0f, 51.0f, -11.0f});
    const render::CameraGpuData logical[]{first.getGpuData(), second.getGpuData()};
    require(first.getProjectionMatrix()[1][1] > 0.0f, "Camera must keep logical Y-up projection");
    const auto nearClip = first.getProjectionMatrix() * glm::vec4{0, 0, -first.getConfig().nearPlane, 1};
    const auto farClip = first.getProjectionMatrix() * glm::vec4{0, 0, -first.getConfig().farPlane, 1};
    require(std::abs(nearClip.z / nearClip.w) < 0.0001f &&
            std::abs(farClip.z / farClip.w - 1.0f) < 0.0001f,
            "logical projection must use zero-to-one depth");
    require(close(first.makeRenderView().viewProjectionMatrix, logical[0].viewProjection),
            "render snapshot must retain logical camera transform");

    CorrectedDevice vulkanConvention;
    vulkanConvention.convention.correction[5] = -1.0f;
    render::FrameBindings frames;
    frames.create(vulkanConvention, 2);
    frames.setCameraData(logical, 2);
    frames.sync(0);
    frames.sync(1);
    for (uint32_t frame = 0; frame < 2; ++frame)
    {
        require(close(uploaded(frames, frame, 0).viewProjection, oldVulkanViewProjection(first)),
                "Vulkan correction must match old projection * rotated/translated view");
        require(close(uploaded(frames, frame, 1).viewProjection, oldVulkanViewProjection(second)),
                "all cameras in all replicas must receive backend correction");
        require(uploaded(frames, frame, 0).worldPosition == logical[0].worldPosition,
                "clip correction must preserve world camera position");
    }
    frames.setCameraData(logical, 2);
    frames.sync(0);
    require(close(uploaded(frames, 0, 0).viewProjection, oldVulkanViewProjection(first)),
            "repeated updates must not accumulate correction");
    require(close(first.getViewProjectionMatrix(), logical[0].viewProjection),
            "upload must not mutate CPU camera data");

    RecordingDevice identityDevice;
    frames.create(identityDevice, 1);
    frames.setCameraData(logical, 2);
    frames.sync(0);
    require(close(uploaded(frames, 0, 0).viewProjection, logical[0].viewProjection),
            "default Device convention must leave camera data unchanged");

    // A non-diagonal correction catches transposition and right-multiplication;
    // a Y reflection alone cannot distinguish row-major storage mistakes.
    CorrectedDevice general;
    general.convention.correction[1] = 0.25f;
    general.convention.correction[14] = 0.5f;
    glm::mat4 correction{1.0f};
    correction[0][1] = 0.25f;
    correction[3][2] = 0.5f;
    frames.create(general, 1);
    frames.setCameraData(logical[0]);
    frames.sync(0);
    require(close(uploaded(frames, 0, 0).viewProjection, correction * logical[0].viewProjection),
            "column-major correction must left-multiply the complete logical VP");

    const auto logicalFrustum = math::Frustum::fromViewProjection(logical[0].viewProjection);
    const auto oldFrustum = math::Frustum::fromViewProjection(oldVulkanViewProjection(first));
    uint32_t accepted = 0, rejected = 0;
    for (float depth : {-4.0f, 0.05f, 2.0f, 15.0f, 80.0f})
        for (float horizontal : {-50.0f, -1.0f, 0.0f, 1.0f, 50.0f})
            for (float vertical : {-40.0f, 0.0f, 40.0f})
            {
                const auto center = first.getPosition() + depth * first.getForwardVector() +
                    horizontal * first.getRightVector() + vertical * first.getUpVector();
                const math::Aabb bounds{center - glm::vec3{0.01f}, center + glm::vec3{0.01f}};
                const bool visible = logicalFrustum.intersects(bounds);
                require(visible == oldFrustum.intersects(bounds),
                        "logical CPU culling must match the former flipped projection");
                visible ? ++accepted : ++rejected;
            }
    require(accepted != 0 && rejected != 0, "frustum fixture must exercise both visibility outcomes");
    const auto aboveCenter = first.getPosition() + 5.0f * first.getForwardVector() + first.getUpVector();
    const auto clip = logical[0].viewProjection * glm::vec4{aboveCenter, 1.0f};
    require(clip.y / clip.w > 0.0f, "logical screen projection must keep positive Y above center");
}
} // namespace
int main()
{
    try { run(); std::cout << "camera clip-space tests passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
