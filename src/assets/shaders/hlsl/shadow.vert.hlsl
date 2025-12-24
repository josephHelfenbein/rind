#pragma pack_matrix(row_major)

struct VSInput {
    [[vk::location(0)]] float3 inPosition : POSITION;
};

struct VSOutput {
    float4 gl_Position : SV_Position;
};

struct PushConstants {
    float4x4 model;
    float4x4 viewProj;
    float4 lightPos; // xyz = pos, w = radius (far plane)
};

[[vk::push_constant]] PushConstants pc;

VSOutput main(VSInput input) {
    VSOutput output;
    float4 worldPos = mul(float4(input.inPosition, 1.0), pc.model);
    float4 clipPos = mul(worldPos, pc.viewProj);
    float distance = length(worldPos.xyz - pc.lightPos.xyz);
    float linearDepth = clamp(distance / pc.lightPos.w, 0.0, 1.0);
    output.gl_Position = float4(clipPos.x, clipPos.y, linearDepth * clipPos.w, clipPos.w);
    return output;
}
