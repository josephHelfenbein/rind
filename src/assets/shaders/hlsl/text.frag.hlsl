struct VSOutput {
    [[vk::location(0)]] float2 texCoord : TEXCOORD0;
};

[[vk::binding(0)]]
[[vk::combinedImageSampler]]
Texture2D<float4> sampleTexture;

[[vk::binding(0)]]
[[vk::combinedImageSampler]]
SamplerState sampleSampler;

struct PushConstants {
    float3 tint;
    float4x4 model;
};
[[vk::push_constant]] PushConstants pc;

float4 main(VSOutput input) : SV_Target {
    float2 glyphUV = float2(input.texCoord.x, 1.0 - input.texCoord.y);
    float alpha = sampleTexture.Sample(sampleSampler, glyphUV).r;
    if(alpha < 0.01) discard;
    return float4(pc.tint, alpha);
}
