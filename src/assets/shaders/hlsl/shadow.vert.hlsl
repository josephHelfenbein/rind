#pragma pack_matrix(row_major)

struct VSInput {
    [[vk::location(0)]] float3 inPosition : POSITION;
};

struct VSOutput {
    float4 gl_Position : SV_Position;
    [[vk::location(0)]] float linearDepth : TEXCOORD0;
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
    float distance = length(worldPos.xyz - pc.lightPos.xyz);
    float linearDepth = clamp(distance / pc.lightPos.w, 0.0, 1.0);
    output.linearDepth = linearDepth;
    output.gl_Position = mul(worldPos, pc.viewProj);
    return output;
}
