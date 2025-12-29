struct VSOutput {
    [[vk::location(0)]] float2 fragTexCoord : TEXCOORD0;
};

[[vk::binding(0)]]
Texture2D<float4> sceneTexture;

[[vk::binding(1)]]
Texture2D<float4> uiTexture;

[[vk::binding(2)]]
Texture2D<float4> textTexture;

[[vk::binding(3)]]
Texture2D<float4> ssrTexture;

[[vk::binding(4)]]
SamplerState sampleSampler;

float3 ACESFilm(float3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float4 main(VSOutput input) : SV_Target {
    float4 sceneColor = sceneTexture.Sample(sampleSampler, input.fragTexCoord);
    float4 uiColor = uiTexture.Sample(sampleSampler, input.fragTexCoord);
    float4 textColor = textTexture.Sample(sampleSampler, input.fragTexCoord);
    float4 ssrColor = ssrTexture.Sample(sampleSampler, input.fragTexCoord);

    float3 tonemapped = ACESFilm(sceneColor.rgb + ssrColor.rgb * ssrColor.a);
    float4 sceneUI = lerp(float4(tonemapped, sceneColor.a), uiColor, uiColor.a);
    return lerp(sceneUI, textColor, textColor.a);
}