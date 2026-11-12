// Full-screen triangle used by the final presentation pass.
//
// Vulkan's positive-height viewport maps NDC y = -1 to the top of the
// framebuffer. Keeping UV (0, 0) at that vertex samples an offscreen image
// rendered with the same viewport orientation without an extra Y flip.

struct PresentVertexOutput
{
    float4 position : SV_Position;

    [[vk::location(0)]]
    float2 texCoord : TEXCOORD0;
};

PresentVertexOutput main(uint vertexId : SV_VulkanVertexID)
{
    // Generates (-1,-1), (3,-1), (-1,3). The oversized triangle avoids the
    // diagonal interpolation seam of a full-screen quad.
    const float2 texCoord = float2(
        (vertexId << 1) & 2,
        vertexId & 2);

    PresentVertexOutput output;
    output.position = float4(texCoord * 2.0f - 1.0f, 0.0f, 1.0f);
    output.texCoord = texCoord;
    return output;
}
