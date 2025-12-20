struct VSOutput {
    [[vk::location(0)]] float3 fragPosition : TEXCOORD0;
    [[vk::location(1)]] float3 fragNormal : TEXCOORD1;
    [[vk::location(2)]] float2 fragTexCoord : TEXCOORD2;
    [[vk::location(3)]] float3x3 fragTBN : TEXCOORD3;
};

struct GBufferOutput {
    [[vk::location(0)]] float4 outAlbedo;
    [[vk::location(1)]] float4 outNormal;
    [[vk::location(2)]] float4 outMaterial;
};

[[vk::binding(0)]]
Texture2D<float4> albedoTexture;

[[vk::binding(1)]]
Texture2D<float4> metallicTexture;

[[vk::binding(2)]]
Texture2D<float4> roughnessTexture;

[[vk::binding(3)]]
Texture2D<float4> normalTexture;

[[vk::binding(4)]]
SamplerState sampleSampler;

float3 getNormalFromMap(float3 normalMap, float3x3 TBN) {
    float3 N = normalize(mul(TBN, normalMap));
    return N;
}

GBufferOutput main(VSOutput input) : SV_Target {
    float4 baseColor = albedoTexture.Sample(sampleSampler, input.fragTexCoord);
    float4 metallicRaw = metallicTexture.Sample(sampleSampler, input.fragTexCoord);
    float mask = metallicRaw.a;
    if (mask < 0.01) discard;
    float metallic = metallicRaw.r;
    float roughness = max(roughnessTexture.Sample(sampleSampler, input.fragTexCoord).r, 0.01);
    float3 normal = getNormalFromMap(normalTexture.Sample(sampleSampler, input.fragTexCoord).xyz * 2.0 - 1.0, input.fragTBN);
    GBufferOutput output;
    output.outAlbedo = baseColor;
    output.outNormal = float4(normalize(normal) * 0.5 + 0.5, 1.0);
    output.outMaterial = float4(metallic, roughness, 0.0, 1.0);
    return output;
}