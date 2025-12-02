struct VSOutput {
    float4 gl_Position : SV_Position;
    [[vk::location(0)]] float3 fragPosition : TEXCOORD0;
    [[vk::location(1)]] float3 fragNormal : TEXCOORD1;
    [[vk::location(2)]] float2 fragTexCoord : TEXCOORD2;
    [[vk::location(3)]] float3x3 fragTBN : TEXCOORD3;
};

struct VSInput {
    [[vk::location(0)]] float3 inPosition : POSITION;
    [[vk::location(1)]] float3 inNormal : NORMAL;
    [[vk::location(2)]] float2 inTexCoord : TEXCOORD0;
    [[vk::location(3)]] float3 inTangent : TANGENT;
};

struct PushConstants {
    float4x4 model;
    float4x4 view;
    float4x4 projection;
    float3 camPos;
};
[[vk::push_constant]] PushConstants pc;

VSOutput main(VSInput input) {
    float4 worldPos = mul(pc.model, float4(input.inPosition, 1.0));
    float3 T = normalize(mul((float3x3)pc.model, input.inTangent));
    float3 N = normalize(mul((float3x3)pc.model, input.inNormal));
    T = normalize(T - dot(T, N) * N);
    float3 B = cross(N, T);
    VSOutput output;
    output.gl_Position = mul(pc.projection, mul(pc.view, worldPos)),
    output.fragPosition = worldPos.xyz,
    output.fragNormal = N,
    output.fragTexCoord = input.inTexCoord,
    output.fragTBN = float3x3(T, B, N)
    return output;
}