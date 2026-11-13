// Import twice: Vertex / VSMain and Pixel / PSMain. Pair colorTexture with colorSampler.
#include "RenderData.hlsli"

struct VertexInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(3)]] float2 uv : TEXCOORD0;
};
struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};
struct MaterialParameters { float4 tint; };
[[vk::binding(0, 1)]] ConstantBuffer<MaterialParameters> materialData;
[[vk::binding(1, 1)]] Texture2D<float4> colorTexture;
[[vk::binding(2, 1)]] SamplerState colorSampler;

VertexOutput VSMain(VertexInput input)
{
    const ObjectGpuData object = objectData[drawPushConstants.objectIndex];
    const CameraGpuData camera = cameraBuffer.cameras[drawPushConstants.cameraIndex];
    VertexOutput output;
    output.position = mul(camera.viewProjection, mul(object.world, float4(input.position, 1.0)));
    output.uv = input.uv;
    return output;
}
float4 PSMain(VertexOutput input) : SV_Target
{
    return colorTexture.Sample(colorSampler, input.uv) * materialData.tint;
}
