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
    float4x4 viewProj;
    float4 lightPos; // xyz = pos, w = radius (far plane)
    uint flags;
    uint pad0;
    uint pad1;
    uint pad2;
};

[[vk::push_constant]] PushConstants pc;

struct JointMatricesUBO {
    float4x4 jointMatrices[128];
};
[[vk::binding(0, 0)]] ConstantBuffer<JointMatricesUBO> joints;

VSOutput main(VSInput input) {
    VSOutput output;
    float3 skinnedPos = input.inPosition;
    
    if ((pc.flags & 1) != 0) {
        float4x4 skinMatrix = mul(input.inWeights.x, joints.jointMatrices[uint(input.inJoints.x)]) +
                            mul(input.inWeights.y, joints.jointMatrices[uint(input.inJoints.y)]) +
                            mul(input.inWeights.z, joints.jointMatrices[uint(input.inJoints.z)]) +
                            mul(input.inWeights.w, joints.jointMatrices[uint(input.inJoints.w)]);
        skinnedPos = mul(float4(input.inPosition, 1.0), skinMatrix).xyz;
    }
    
    float4 worldPos = mul(float4(skinnedPos, 1.0), pc.model);
    float4 clipPos = mul(worldPos, pc.viewProj);
    float distance = length(worldPos.xyz - pc.lightPos.xyz);
    float linearDepth = clamp(distance / pc.lightPos.w, 0.0, 1.0);
    output.gl_Position = float4(clipPos.x, clipPos.y, linearDepth * clipPos.w, clipPos.w);
    return output;
}
