#pragma pack_matrix(row_major)

struct VSOutput {
    [[vk::location(0)]] float2 fragTexCoord : TEXCOORD2;
};

[[vk::binding(0)]]
Texture2D<float4> srcTexture;

[[vk::binding(1)]]
SamplerState sampleSampler;

struct PushConstants {
    float2 halfPixel;
    uint pad[2];
};
[[vk::push_constant]] PushConstants pc;

float4 main(VSOutput input) : SV_Target {
    float2 uv = input.fragTexCoord;
    float2 halfPixel = pc.halfPixel;

    float4 sum = srcTexture.SampleLevel(sampleSampler, uv, 0) * 4.0;
    sum += srcTexture.SampleLevel(sampleSampler, uv + float2(-halfPixel.x,  halfPixel.y), 0);
    sum += srcTexture.SampleLevel(sampleSampler, uv + float2( halfPixel.x,  halfPixel.y), 0);
    sum += srcTexture.SampleLevel(sampleSampler, uv + float2(-halfPixel.x, -halfPixel.y), 0);
    sum += srcTexture.SampleLevel(sampleSampler, uv + float2( halfPixel.x, -halfPixel.y), 0);
    return float4(sum.rgb / 8.0, 1.0);
}
