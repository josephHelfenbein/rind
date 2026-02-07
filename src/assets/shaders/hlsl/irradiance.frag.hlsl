struct VSOutput {
    [[vk::location(0)]] float3 worldNormal : TEXCOORD0;
    [[vk::location(1)]] float2 uv : TEXCOORD1;
};

[[vk::binding(1)]]
Texture2D<float4> albedoTexture;

[[vk::binding(2)]]
Texture2D<float4> metallicTexture;

[[vk::binding(3)]]
Texture2D<float4> roughnessTexture;

[[vk::binding(4)]]
Texture2D<float4> normalTexture;

[[vk::binding(5)]]
SamplerState sampleSampler;

float4 main(VSOutput input) : SV_Target {
    float3 albedo = albedoTexture.Sample(sampleSampler, input.uv).rgb;
    float3 upDir = float3(0, 1, 0);
    return float4(albedo, 1.0);
}