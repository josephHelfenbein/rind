struct VSOutput {
    [[vk::location(0)]] float2 texCoord : TEXCOORD0;
};

[[vk::binding(0)]]
[[vk::combinedImageSampler]]
Texture2D<float4> sceneTexture;

[[vk::binding(1)]]
[[vk::combinedImageSampler]]
Texture2D<float4> uiTexture;

[[vk::binding(0)]]
[[vk::combinedImageSampler]]
SamplerState sampleSampler;

vec4 main(VSOutput input) : SV_Target {
    float4 sceneColor = sceneTexture.Sample(sampleSampler, input.texCoord);
    float4 uiColor = uiTexture.Sample(sampleSampler, input.texCoord);
    
    float alpha = uiColor.a;
    return lerp(sceneColor, uiColor, alpha);
}