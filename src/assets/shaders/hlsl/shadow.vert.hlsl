#pragma pack_matrix(row_major)

struct VSInput {
    [[vk::location(0)]] float3 inPosition : POSITION;
    [[vk::location(1)]] float4 inJoints : JOINTS;
    [[vk::location(2)]] float4 inWeights : WEIGHTS;
};

struct VSOutput {
    float4 gl_Position : SV_Position;
};

struct PushConstants {
    float4x4 model;
    uint lightIndex;
    uint flags;
    uint pad0;
    uint pad1;
};

[[vk::push_constant]] PushConstants pc;

struct JointMatricesUBO {
    float4x4 jointMatrices[128];
};
[[vk::binding(0, 0)]] ConstantBuffer<JointMatricesUBO> joints;

struct ShadowLightEntry {
    float4x4 viewProjs[6];
    float4 lightPosRadius; // xyz = pos, w = radius
};
[[vk::binding(1, 0)]] StructuredBuffer<ShadowLightEntry> shadowLights;

VSOutput main(VSInput input, uint viewId : SV_ViewID) {
    VSOutput output;
    float3 skinnedPos = input.inPosition;

    if ((pc.flags & 1) != 0) {
        float4x4 skinMatrix = mul(input.inWeights.x, joints.jointMatrices[uint(input.inJoints.x)]) +
                            mul(input.inWeights.y, joints.jointMatrices[uint(input.inJoints.y)]) +
                            mul(input.inWeights.z, joints.jointMatrices[uint(input.inJoints.z)]) +
                            mul(input.inWeights.w, joints.jointMatrices[uint(input.inJoints.w)]);
        skinnedPos = mul(float4(input.inPosition, 1.0), skinMatrix).xyz;
    }

    ShadowLightEntry light = shadowLights[pc.lightIndex];
    float4 worldPos = mul(float4(skinnedPos, 1.0), pc.model);
    float4 clipPos = mul(worldPos, light.viewProjs[viewId]);
    float distance = length(worldPos.xyz - light.lightPosRadius.xyz);
    float linearDepth = clamp(distance / light.lightPosRadius.w, 0.0, 1.0);
    output.gl_Position = float4(clipPos.x, clipPos.y, linearDepth * clipPos.w, clipPos.w);
    return output;
}
