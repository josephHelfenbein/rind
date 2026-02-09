#pragma pack_matrix(row_major)

struct VSOutput {
    [[vk::location(0)]] float2 fragTexCoord : TEXCOORD2;
};

[[vk::binding(0)]]
Texture2D<float4> lightingTexture;

[[vk::binding(1)]]
SamplerState sampleSampler;

float4 main(VSOutput input) : SV_Target {
    float2 uv = input.fragTexCoord;
    float4 color = lightingTexture.Sample(sampleSampler, uv);
    float brightness = dot(color.rgb, float3(0.2126, 0.7152, 0.0722));
    float bloomIntensity = saturate((brightness - 1.0) * 5.0);
    if (bloomIntensity > 0.0) {
        return float4(color.rgb * bloomIntensity, 1.0);
    } else {
        return float4(0.0, 0.0, 0.0, 1.0);
    }
}