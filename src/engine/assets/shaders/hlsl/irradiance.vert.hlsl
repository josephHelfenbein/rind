#pragma pack_matrix(row_major)

struct VSOutput {
    float4 gl_Position : SV_Position;
    [[vk::location(0)]] float3 worldNormal : TEXCOORD0;
    [[vk::location(1)]] float2 uv : TEXCOORD1;
};

struct VSInput {
    [[vk::location(0)]] float3 inPosition : POSITION;
    [[vk::location(1)]] float3 inNormal : NORMAL;
    [[vk::location(2)]] float2 inTexCoord : TEXCOORD0;
};

struct PushConstants {
    float4x4 model;
    float4x4 viewProj;
};
[[vk::push_constant]] PushConstants pc;

struct JointMatricesUBO {
    float4x4 joints[128];
};
[[vk::binding(0)]] ConstantBuffer<JointMatricesUBO> jointUBO;

VSOutput main(VSInput input) {
    VSOutput output;
    float4 worldPos = mul(float4(input.inPosition, 1.0), pc.model);
    output.gl_Position = mul(worldPos, pc.viewProj);
    output.worldNormal = normalize(mul(float4(input.inNormal, 0.0), pc.model).xyz);
    output.uv = input.inTexCoord;
    return output;
}