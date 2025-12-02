struct VSInput {
    [[vk::location(0)]] float2 inPosition : POSITION;
    [[vk::location(1)]] float2 inTexCoords : TEXCOORD0;
};

struct VSOutput {
    float4 gl_Position : SV_Position;
    [[vk::location(0)]] float2 texCoord : TEXCOORD0;
};

struct PushConstants {
    float3 tint;
    float4x4 model;
};

[[vk::push_constant]] PushConstants pc;

VSOutput main(VSInput input) {
    VSOutput output;
    output.texCoord = input.inTexCoords;
    output.gl_Position = mul(pc.model, float4(input.inPosition, 0.0, 1.0));
    return output;
}
