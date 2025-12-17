struct VSOutput {
    [[vk::location(0)]] float2 texCoord : TEXCOORD0;
};

[[vk::binding(0)]]
Texture2D<float4> sceneTexture;

[[vk::binding(1)]]
Texture2D<float4> uiTexture;

[[vk::binding(2)]]
Texture2D<float4> textTexture;

[[vk::binding(3)]]
SamplerState sampleSampler;

float4 main(VSOutput input) : SV_Target {
    float4 sceneColor = sceneTexture.Sample(sampleSampler, input.texCoord);
    float4 uiColor = uiTexture.Sample(sampleSampler, input.texCoord);
    float4 textColor = textTexture.Sample(sampleSampler, input.texCoord);
    
    float4 sceneUI = lerp(sceneColor, uiColor, uiColor.a);
    return lerp(sceneUI, textColor, textColor.a);
}