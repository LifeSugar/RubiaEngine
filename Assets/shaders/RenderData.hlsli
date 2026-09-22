#ifndef LEARN_VULKAN_RENDER_DATA_HLSLI
#define LEARN_VULKAN_RENDER_DATA_HLSLI

#pragma pack_matrix(column_major)

struct CameraGpuData
{
    float4x4 viewProjection;
    float4 worldPosition;
};

static const uint MAX_CAMERA_COUNT = 16;

struct CameraBufferGpuData
{
    CameraGpuData cameras[MAX_CAMERA_COUNT];
};

struct ObjectGpuData
{
    float4x4 world;
    float4x4 normalMatrix;
};

struct DrawPushConstants
{
    uint cameraIndex;
    uint objectIndex;
    float alphaClipThreshold;
};

// set 0 / binding 0 is reserved for future per-frame constants.
[[vk::binding(1, 0)]]
ConstantBuffer<CameraBufferGpuData> cameraBuffer : register(b1, space0);

[[vk::binding(2, 0)]]
StructuredBuffer<ObjectGpuData> objectData : register(t2, space0);

[[vk::push_constant]]
ConstantBuffer<DrawPushConstants> drawPushConstants;

#endif
