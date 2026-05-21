struct VSOutput {
    [[vk::location(0)]] float2 texCoord : TEXCOORD0;
};

[[vk::binding(0)]]
Texture2D<float4> sampleTexture;

[[vk::binding(1)]]
SamplerState sampleSampler;

struct PushConstants {
    float4x4 model;
    float4 tint;
    float4 uvClip;
};

[[vk::push_constant]] PushConstants pc;

float4 main(VSOutput input) : SV_Target {
    if (input.texCoord.x < pc.uvClip.x || input.texCoord.x > pc.uvClip.z ||
        input.texCoord.y < pc.uvClip.y || input.texCoord.y > pc.uvClip.w) {
        discard;
    }
    return sampleTexture.Sample(sampleSampler, input.texCoord) * pc.tint;
}
