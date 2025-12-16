struct VSOutput {
    [[vk::location(0)]] float2 texCoord : TEXCOORD0;
};

[[vk::binding(0)]]
[[vk::sampled_image]]
Texture2D<float4> sampleTexture;

[[vk::binding(1)]]
SamplerState sampleSampler;

struct PushConstants {
    float3 tint;
    float4x4 model;
};
[[vk::push_constant]] PushConstants pc;

float4 main(VSOutput input) : SV_Target {
    return sampleTexture.Sample(sampleSampler, input.texCoord) * float4(pc.tint, 1.0);
}
