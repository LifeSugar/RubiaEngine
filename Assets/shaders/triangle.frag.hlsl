// Fragment shader: simple PBR-style direct lighting with a pale orange material.

#include "RenderData.hlsli"

// Scene shader ABI: fragment bool specialization 1000, threshold in DrawPushConstants.
[[vk::constant_id(1000)]] const bool rubiaAlphaClipEnabled = false;

struct PSInput
{
    float4 position : SV_Position;

    [[vk::location(0)]]
    float3 worldPosition : TEXCOORD0;

    [[vk::location(1)]]
    float3 normal        : NORMAL;

    [[vk::location(2)]]
    float4 color         : COLOR0;

    [[vk::location(3)]]
    float2 texCoord      : TEXCOORD1;

    [[vk::location(4)]]
    float4 tangent       : TANGENT;
};

struct MaterialGpuData
{
    float4 baseColorFactor;
    float3 emissiveFactor;
    float metallicFactor;
    float roughnessFactor;
};

[[vk::binding(0, 1)]]
ConstantBuffer<MaterialGpuData> materialData;

[[vk::binding(1, 1)]] Texture2D<float4> baseColorTexture;
[[vk::binding(2, 1)]] Texture2D<float4> metallicRoughnessTexture;
[[vk::binding(3, 1)]] Texture2D<float4> normalTexture;
[[vk::binding(4, 1)]] Texture2D<float4> occlusionTexture;
[[vk::binding(5, 1)]] Texture2D<float4> emissiveTexture;

[[vk::binding(6, 1)]] SamplerState baseColorSampler;
[[vk::binding(7, 1)]] SamplerState metallicRoughnessSampler;
[[vk::binding(8, 1)]] SamplerState normalSampler;
[[vk::binding(9, 1)]] SamplerState occlusionSampler;
[[vk::binding(10, 1)]] SamplerState emissiveSampler;

static const float PI = 3.14159265359f;

float DistributionGGX(float3 normal, float3 halfVector, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float nDotH = max(dot(normal, halfVector), 0.0f);
    float nDotH2 = nDotH * nDotH;
    float denom = nDotH2 * (a2 - 1.0f) + 1.0f;
    return a2 / max(PI * denom * denom, 0.0001f);
}

float GeometrySchlickGGX(float nDotV, float roughness)
{
    float r = roughness + 1.0f;
    float k = (r * r) / 8.0f;
    return nDotV / max(nDotV * (1.0f - k) + k, 0.0001f);
}

float GeometrySmith(float3 normal, float3 viewDir, float3 lightDir, float roughness)
{
    float nDotV = max(dot(normal, viewDir), 0.0f);
    float nDotL = max(dot(normal, lightDir), 0.0f);
    return GeometrySchlickGGX(nDotV, roughness) * GeometrySchlickGGX(nDotL, roughness);
}

float3 FresnelSchlick(float cosTheta, float3 f0)
{
    return f0 + (1.0f - f0) * pow(saturate(1.0f - cosTheta), 5.0f);
}

float3 SampleWorldNormal(PSInput input)
{
    const float3 vertexNormal = normalize(input.normal);
    const float3 tangent = normalize(
        input.tangent.xyz -
        vertexNormal * dot(vertexNormal, input.tangent.xyz));
    const float3 bitangent =
        normalize(cross(vertexNormal, tangent)) * input.tangent.w;

    // BC5 stores tangent-space X/Y in unsigned RG. This decode also works for
    // the current RGBA source texture until it is reimported as BC5.
    const float2 encodedXY =
        normalTexture.Sample(normalSampler, input.texCoord).rg;
    const float2 tangentXY = encodedXY * 2.0f - 1.0f;
    const float tangentZ = sqrt(saturate(
        1.0f - dot(tangentXY, tangentXY)));
    const float3 tangentNormal = float3(tangentXY, tangentZ);

    return normalize(
        tangentNormal.x * tangent +
        tangentNormal.y * bitangent +
        tangentNormal.z * vertexNormal);
}

float4 main(PSInput input) : SV_Target
{
    const float4 sampledBaseColor =
        baseColorTexture.Sample(baseColorSampler, input.texCoord);
    const float alpha = sampledBaseColor.a * materialData.baseColorFactor.a * input.color.a;
    if (rubiaAlphaClipEnabled)
    {
        clip(alpha - drawPushConstants.alphaClipThreshold);
    }
    const float4 sampledMetallicRoughness =
        metallicRoughnessTexture.Sample(
            metallicRoughnessSampler,
            input.texCoord);
    const float occlusion =
        occlusionTexture.Sample(occlusionSampler, input.texCoord).r;
    const float3 emissive =
        emissiveTexture.Sample(emissiveSampler, input.texCoord).rgb *
        materialData.emissiveFactor;
    const float3 baseColor =
        sampledBaseColor.rgb *
        materialData.baseColorFactor.rgb *
        input.color.rgb;
    const float metallic = saturate(
        materialData.metallicFactor * sampledMetallicRoughness.b);
    const float roughness = clamp(
        materialData.roughnessFactor * sampledMetallicRoughness.g,
        0.04f,
        1.0f);
    const float3 cameraPosition =
        cameraBuffer.cameras[drawPushConstants.cameraIndex].worldPosition.xyz;
    const float3 lightPosition = float3(2.5f, 3.5f, 2.0f);
    const float3 lightColor = float3(5.0f, 4.6f, 4.2f);
    const float3 ambientColor = float3(0.12f, 0.11f, 0.10f);

    float3 normal = SampleWorldNormal(input);
    float3 viewDir = normalize(cameraPosition - input.worldPosition);
    float3 lightDir = normalize(lightPosition - input.worldPosition);
    float3 halfVector = normalize(viewDir + lightDir);

    float distanceToLight = length(lightPosition - input.worldPosition);
    float attenuation = 1.0f / max(distanceToLight * distanceToLight, 0.0001f);
    float3 radiance = lightColor * attenuation;

    float3 f0 = lerp(float3(0.04f, 0.04f, 0.04f), baseColor, metallic);
    float normalDistribution = DistributionGGX(normal, halfVector, roughness);
    float geometry = GeometrySmith(normal, viewDir, lightDir, roughness);
    float3 fresnel = FresnelSchlick(max(dot(halfVector, viewDir), 0.0f), f0);

    float3 numerator = normalDistribution * geometry * fresnel;
    float denominator = 4.0f * max(dot(normal, viewDir), 0.0f) * max(dot(normal, lightDir), 0.0f);
    float3 specular = numerator / max(denominator, 0.0001f);

    float3 kS = fresnel;
    float3 kD = (1.0f - kS) * (1.0f - metallic);
    float nDotL = max(dot(normal, lightDir), 0.0f);

    float3 ambient = ambientColor * baseColor * occlusion;
    float3 color = ambient + (kD * baseColor / PI + specular) * radiance * nDotL;
    color += emissive;

    return float4(
        color,
        alpha);
}
