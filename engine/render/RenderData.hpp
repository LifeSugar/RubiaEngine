#pragma once

#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>

namespace rubia::render
{

    /// Maximum number of camera records stored in one frame's uniform buffer.
    inline constexpr uint32_t MaxCameraCount = 16;

    // One snapshot per CPU/GPU frame slot. Keep the structure 16-byte aligned so
    // it can be copied directly into a uniform-buffer allocation.
    struct alignas(16) FrameGpuData
    {
        /// Elapsed application time in seconds.
        float time = 0.0f;
        /// Time elapsed since the previous frame in seconds.
        float deltaTime = 0.0f;
        /// Monotonic frame number.
        uint32_t frameIndex = 0;
        /// Padding required by the GPU data layout.
        uint32_t padding = 0;
    };

    // Camera remains the CPU-side source of truth. This is only the compact data
    // snapshot consumed by shaders for a camera render flow.
    struct alignas(16) CameraGpuData
    {
        /// Combined view and projection transform.
        glm::mat4 viewProjection{1.0f};
        /// Camera position in world space.
        glm::vec4 worldPosition{0.0f, 0.0f, 0.0f, 1.0f};
    };

    // Per-object data is uploaded as a contiguous array for the current frame.
    // DrawPushConstants::objectIndex selects an entry from that array.
    struct alignas(16) ObjectGpuData
    {
        /// Object-to-world transform.
        glm::mat4 world{1.0f};
        /// Transform used for surface normals.
        glm::mat4 normalMatrix{1.0f};
    };

    // Reserved fragment specialization ID. Shaders using it opt into the scene
    // alpha-clip ABI: bool enable plus float threshold at push-constant offset 8.
    inline constexpr uint32_t AlphaClipSpecializationId = 1000;

    // Small, frequently changing indices used to select the camera and object
    // records while recording a draw.
    struct DrawPushConstants
    {
        /// Camera-data slot selected by the draw.
        uint32_t cameraIndex = 0;
        /// Object-data slot selected by the draw.
        uint32_t objectIndex = 0;
        /// Material data: changing this value does not create a pipeline variant.
        float alphaClipThreshold = 0.5f;
    };

    /// Parameters consumed by the final scene-color presentation shader.
    struct alignas(16) PresentPushConstants
    {
        float exposureEv = 0.0f;
        uint32_t toneMappingMode = 1;
        uint32_t outputTransferFunction = 0;
        uint32_t padding = 0;
    };

    static_assert(sizeof(FrameGpuData) == 16);
    static_assert(alignof(FrameGpuData) == 16);
    static_assert(sizeof(CameraGpuData) == 80);
    static_assert(alignof(CameraGpuData) == 16);
    static_assert(offsetof(CameraGpuData, worldPosition) == 64);
    static_assert(sizeof(CameraGpuData) * MaxCameraCount <= 16 * 1024);
    static_assert(sizeof(ObjectGpuData) == 128);
    static_assert(alignof(ObjectGpuData) == 16);
    static_assert(offsetof(ObjectGpuData, normalMatrix) == 64);
    static_assert(sizeof(DrawPushConstants) == 12);
    static_assert(offsetof(DrawPushConstants, alphaClipThreshold) == 8);
    static_assert(sizeof(PresentPushConstants) == 16);
    static_assert(alignof(PresentPushConstants) == 16);

} // namespace rubia::render
