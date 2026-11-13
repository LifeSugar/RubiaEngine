#include "../shaders/RenderData.hlsli"

// Different source input signatures; the current importer still uses asset::Vertex storage.
struct Varyings {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
    [[vk::location(1)]] float4 color : COLOR0;
    [[vk::location(2)]] float3 normal : NORMAL0;
    [[vk::location(3)]] float4 tangent : TANGENT0;
    [[vk::location(4)]] float3 world : TEXCOORD4;
};
Varyings transformVertex(float3 p, float2 uv, float4 color, float3 normal, float4 tangent) {
    ObjectGpuData obj = objectData[drawPushConstants.objectIndex];
    Varyings o;
    o.position = mul(cameraBuffer.cameras[drawPushConstants.cameraIndex].viewProjection,
                     mul(obj.world, float4(p, 1)));
    o.world = mul(obj.world,float4(p,1)).xyz;
    o.uv = uv; o.color = color;
    o.normal = normalize(mul((float3x3)obj.normalMatrix, normal));
    o.tangent = float4(normalize(mul((float3x3)obj.world, tangent.xyz)), tangent.w);
    return o;
}
Varyings VSBasic([[vk::location(0)]] float3 p : POSITION) {
    return transformVertex(p, p.xy * .5 + .5, 1, normalize(p + .001), float4(1,0,0,1));
}
Varyings VSColor([[vk::location(0)]] float3 p : POSITION,
                 [[vk::location(1)]] float4 c : COLOR0) {
    return transformVertex(p, p.xy * .5 + .5, c, normalize(p + .001), float4(1,0,0,1));
}
struct FullVertex {
    [[vk::location(0)]] float3 p : POSITION;
    [[vk::location(1)]] float4 color : COLOR0;
    [[vk::location(2)]] float3 n : NORMAL0;
    [[vk::location(3)]] float2 uv : TEXCOORD0;
    [[vk::location(4)]] float4 t : TANGENT0;
};
Varyings VSFull(FullVertex v) { return transformVertex(v.p, v.uv, v.color, v.n, v.t); }
Varyings VSTiled(FullVertex v) { return transformVertex(v.p, v.uv * 2, 1, v.n, v.t); }

// surface: roughness, metallic, texture amount, emission strength (reflected).
struct Parameters { float4 tint; float4 surface; };
[[vk::binding(0, 1)]] ConstantBuffer<Parameters> material;
[[vk::binding(1, 1)]] Texture2D colorTexture;
[[vk::binding(2, 1)]] SamplerState colorSampler;
[[vk::binding(3, 1)]] Texture2D normalTexture;
[[vk::binding(4, 1)]] SamplerState normalSampler;
[[vk::binding(5, 1)]] Texture2D ormTexture;
[[vk::binding(6, 1)]] SamplerState ormSampler;
[[vk::binding(7, 1)]] Texture2D emissionTexture;
[[vk::binding(8, 1)]] SamplerState emissionSampler;
[[vk::constant_id(1000)]] const bool rubiaAlphaClipEnabled = false;
static const float PI = 3.14159265;
float3 safeNormal(float3 n) { return n * rsqrt(max(dot(n,n), 1e-7)); }
float3 viewDirection(Varyings i) { return safeNormal(cameraBuffer.cameras[drawPushConstants.cameraIndex].worldPosition.xyz-i.world); }
float3 fresnel(float cosTheta,float3 f0) { return f0+(1-f0)*pow(1-saturate(cosTheta),5); }
float3 brdf(float3 n,float3 v,float3 l,float3 base,float rough,float metal) {
    float3 h=safeNormal(v+l);
    float nv=max(dot(n,v),.001), nl=saturate(dot(n,l)), nh=saturate(dot(n,h)), vh=saturate(dot(v,h));
    float a=max(.035,rough*rough), a2=a*a, denom=nh*nh*(a2-1)+1;
    float d=a2/max(PI*denom*denom,1e-5);
    float k=(rough+1)*(rough+1)/8;
    float g=(nv/(nv*(1-k)+k))*(nl/max(nl*(1-k)+k,.001));
    float3 f=fresnel(vh,lerp(.04,base,metal));
    return ((1-f)*(1-metal)*base/PI + d*g*f/max(4*nv*nl,.001))*nl;
}
// Analytic studio illumination, independent of a skybox/IBL/shadow pass.
static const float studioLightIntensity = 0.5;
float3 studio(float3 base,float3 n,float3 v,float rough,float metal) {
    float3 result=brdf(n,v,normalize(float3(-.6,1,.6)),base,rough,metal)*float3(3.5,3.1,2.7);
    result+=brdf(n,v,normalize(float3(.8,.5,-.4)),base,rough,metal)*float3(1.1,1.6,2.0);
    result+=base*(1-metal)*lerp(float3(.10,.12,.13),float3(.28,.34,.38),n.y*.5+.5);
    float3 r=reflect(-v,n);
    float softbox=pow(saturate(dot(r,normalize(float3(-.5,.85,.4)))),lerp(180.0,12.0,rough));
    result+=lerp(.04,base,metal)*softbox*1.8;
    return result * studioLightIntensity;
}
float4 PSSolid(Varyings i) : SV_Target {
    return float4(studio(material.tint.rgb,safeNormal(i.normal),viewDirection(i),material.surface.x,material.surface.y),1);
}
float4 PSColor(Varyings i) : SV_Target {
    return float4(studio(material.tint.rgb*i.color.rgb,safeNormal(i.normal),viewDirection(i),material.surface.x,material.surface.y),1);
}
float4 PSTextured(Varyings i) : SV_Target {
    float4 tex=colorTexture.Sample(colorSampler,i.uv);
    if(rubiaAlphaClipEnabled && tex.a<drawPushConstants.alphaClipThreshold) discard;
    float3 n=safeNormal(i.normal),v=viewDirection(i);
    n=dot(n,v)<0?-n:n;
    float3 base=material.tint.rgb*lerp(1,tex.rgb,material.surface.z);
    return float4(studio(base,n,v,material.surface.x,material.surface.y),1);
}
float4 PSDetail(Varyings i) : SV_Target {
    float3 n=safeNormal(i.normal),t=safeNormal(i.tangent.xyz-n*dot(n,i.tangent.xyz));
    float3 b=cross(n,t)*i.tangent.w;
    float3 sampled=normalTexture.Sample(normalSampler,i.uv).xyz*2-1;
    n=safeNormal(t*sampled.x+b*sampled.y+n*sampled.z);
    float3 orm=ormTexture.Sample(ormSampler,i.uv).rgb;
    float3 base=colorTexture.Sample(colorSampler,i.uv).rgb*material.tint.rgb;
    float3 emission=emissionTexture.Sample(emissionSampler,i.uv).rgb;
    return float4(studio(base,n,viewDirection(i),max(.15,orm.g*material.surface.x),orm.b)*orm.r+emission*material.surface.w,1);
}
float4 PSGlass(Varyings i) : SV_Target {
    float3 n=safeNormal(i.normal),v=viewDirection(i);
    float edge=pow(1-saturate(abs(dot(n,v))),3);
    float3 variation=colorTexture.Sample(colorSampler,i.uv).rgb;
    float3 tint=material.tint.rgb*lerp(.9,1.1,variation);
    float3 reflection=studio(float3(.8,.9,1),n,v,material.surface.x,.85);
    float3 color=lerp(tint,reflection,saturate(.18+edge*.7));
    // Straight alpha blending: center transmits the opaque scene, edges reflect more.
    float alpha=saturate(material.tint.a+edge*.65);
    return float4(color,alpha);
}
float4 PSStage(Varyings i) : SV_Target {
    float3 base=i.color.rgb*material.tint.rgb;
    if(i.uv.x>.4 && i.uv.x<.6) return float4(base,1); // Lettering / brass trim.
    if(i.uv.x<.3) {
        float2 p=i.world.xz;
        float2 g=abs(frac(p*.5-.5)-.5)/max(fwidth(p*.5),.001);
        float gridLine=1-saturate(min(g.x,g.y));
        base*=1-gridLine*.13;
        // Fixture-only soft contact darkening under the ten display plinths.
        float x=p.x-clamp(round(p.x/5.2),-2,2)*5.2;
        float z=abs(p.y)-3.55;
        float2 d=max(abs(float2(x,z))-float2(2.25,2.55),0);
        base*=1-.24*exp(-dot(d,d)*3);
        return float4(base*(.88+.12*saturate(i.normal.y))*studioLightIntensity,1);
    }
    if(i.world.y>.44 && i.world.y<.48 && i.normal.y>.9) {
        // Analytic contact darkening for this fixed display fixture, not a shadow map.
        float2 q=i.world.xz-float2(clamp(round(i.world.x/5.2),-2,2)*5.2,sign(i.world.z)*3.55);
        float2 hero=q-float2(-.2,.5);
        float contact=.3*exp(-dot(hero,hero)*2.1);
        float2 a=q-float2(-1.27,-1.10), b=q-float2(.25,-1.36), c=q-float2(1.45,-.66);
        contact+=.18*(exp(-dot(a,a)*6)+exp(-dot(b,b)*6)+exp(-dot(c,c)*6));
        base*=1-contact;
    }
    return float4(studio(base,safeNormal(i.normal),viewDirection(i),material.surface.x,material.surface.y),1);
}
