#pragma pack_matrix(row_major)

struct VSOutput {
    [[vk::location(0)]] float2 fragTexCoord : TEXCOORD2;
};

[[vk::binding(0)]]
Texture2D<float4> smallerTexture;

[[vk::binding(1)]]
Texture2D<float4> sameSizeTexture;

[[vk::binding(2)]]
SamplerState sampleSampler;

struct PushConstants {
    float2 halfPixel;
    uint pad[2];
};
[[vk::push_constant]] PushConstants pc;

float4 main(VSOutput input) : SV_Target {
    float2 uv = input.fragTexCoord;
    float2 halfPixel = pc.halfPixel;

    float4 sum = smallerTexture.SampleLevel(sampleSampler, uv, 0) * 4.0;
    sum += smallerTexture.SampleLevel(sampleSampler, uv + float2(-halfPixel.x, 0.0), 0) * 2.0;
    sum += smallerTexture.SampleLevel(sampleSampler, uv + float2( halfPixel.x, 0.0), 0) * 2.0;
    sum += smallerTexture.SampleLevel(sampleSampler, uv + float2(0.0, -halfPixel.y), 0) * 2.0;
    sum += smallerTexture.SampleLevel(sampleSampler, uv + float2(0.0,  halfPixel.y), 0) * 2.0;
    sum /= 12.0;

    sum += sameSizeTexture.SampleLevel(sampleSampler, uv, 0);
    return float4(sum.rgb, 1.0);
}
