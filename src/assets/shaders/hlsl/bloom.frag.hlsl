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

    const float threshold = 2.0;
    const float knee = 0.5;
    float soft = brightness - threshold + knee;
    soft = clamp(soft, 0.0, 2.0 * knee);
    soft = soft * soft * (1.0 / (4.0 * knee + 1e-4));
    float contribution = max(soft, brightness - threshold) / max(brightness, 1e-4);

    return float4(color.rgb * contribution, 1.0);
}