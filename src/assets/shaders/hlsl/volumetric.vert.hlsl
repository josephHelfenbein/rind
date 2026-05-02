#pragma pack_matrix(row_major)

struct VSOutput {
    float4 gl_Position : SV_Position;
    [[vk::location(0)]] float3 worldPos : TEXCOORD0;
    [[vk::location(1)]] nointerpolation uint instanceID : TEXCOORD1;
    [[vk::location(2)]] nointerpolation uint maxSteps : TEXCOORD2;
    [[vk::location(3)]] nointerpolation float baseDivs : TEXCOORD3;
    [[vk::location(4)]] nointerpolation uint fbmOctaves : TEXCOORD4;
    [[vk::location(5)]] nointerpolation uint doRefinement : TEXCOORD5;
    [[vk::location(6)]] nointerpolation float ageFade : TEXCOORD6;
};

struct VolumetricData {
    float4x4 model;
    float4x4 invModel;
    float4 color; // rgb = color, a = density
    float age;
    float lifetime;
    float2 pad;
};

[[vk::binding(0)]] StructuredBuffer<VolumetricData> volumes;

struct PushConstants {
    float4x4 viewProj;
    float3 camPos;
    float pad;
};

[[vk::push_constant]] PushConstants pc;

static const float LOD_NEAR = 4.0;
static const float LOD_FAR = 20.0;

VSOutput main(float3 localPos : POSITION, uint instanceID : SV_InstanceID) {
    VolumetricData vol = volumes[instanceID];
    float3 worldPos = mul(float4(localPos, 1.0), vol.model).xyz;

    float3 volCenter = vol.model[3].xyz;
    float camDist = length(volCenter - pc.camPos);
    float lodT = saturate((camDist - LOD_NEAR) / (LOD_FAR - LOD_NEAR));

    float time = vol.age / max(vol.lifetime, 0.0001);
    float x = saturate(1.0 - time);

    VSOutput output;
    output.gl_Position = mul(float4(worldPos, 1.0), pc.viewProj);
    output.worldPos = worldPos;
    output.instanceID = instanceID;
    output.maxSteps = (uint) lerp(32.0, 8.0, lodT);
    output.baseDivs = lerp(24.0, 6.0, lodT);
    output.fbmOctaves = (uint) lerp(4.0, 2.0, lodT);
    output.doRefinement = lodT < 0.5 ? 1u : 0u;
    output.ageFade = x * x * (1.0 + 0.2 * x);
    return output;
}