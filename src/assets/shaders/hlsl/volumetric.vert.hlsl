#pragma pack_matrix(row_major)

struct VSOutput {
    float4 gl_Position : SV_Position;
    [[vk::location(0)]] float3 worldPos : TEXCOORD0;
    [[vk::location(1)]] nointerpolation uint instanceID : TEXCOORD1;
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

VSOutput main(float3 localPos : POSITION, uint instanceID : SV_InstanceID) {
    VolumetricData vol = volumes[instanceID];
    float3 worldPos = mul(float4(localPos, 1.0), vol.model).xyz;
    VSOutput output;
    output.gl_Position = mul(float4(worldPos, 1.0), pc.viewProj);
    output.worldPos = worldPos;
    output.instanceID = instanceID;
    return output;
}