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

float4 main(VSOutput input) : SV_Target {
    float2 uv = input.fragTexCoord;
    uint w, h;
    smallerTexture.GetDimensions(w, h);
    float2 halfPixel = 1.0 / float2(w, h);

    float4 sum = smallerTexture.SampleLevel(sampleSampler, uv + float2(-halfPixel.x * 2.0, 0.0), 0);
    sum += smallerTexture.SampleLevel(sampleSampler, uv + float2(-halfPixel.x,  halfPixel.y), 0) * 2.0;
    sum += smallerTexture.SampleLevel(sampleSampler, uv + float2(0.0,  halfPixel.y * 2.0), 0);
    sum += smallerTexture.SampleLevel(sampleSampler, uv + float2( halfPixel.x,  halfPixel.y), 0) * 2.0;
    sum += smallerTexture.SampleLevel(sampleSampler, uv + float2( halfPixel.x * 2.0, 0.0), 0);
    sum += smallerTexture.SampleLevel(sampleSampler, uv + float2( halfPixel.x, -halfPixel.y), 0) * 2.0;
    sum += smallerTexture.SampleLevel(sampleSampler, uv + float2(0.0, -halfPixel.y * 2.0), 0);
    sum += smallerTexture.SampleLevel(sampleSampler, uv + float2(-halfPixel.x, -halfPixel.y), 0) * 2.0;
    sum /= 12.0;

    sum += sameSizeTexture.SampleLevel(sampleSampler, uv, 0);
    return float4(sum.rgb, 1.0);
}
