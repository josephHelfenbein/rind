struct VSOutput {
    [[vk::location(0)]] float2 fragTexCoord : TEXCOORD0;
};

[[vk::binding(0)]]
Texture2D<float4> sceneTexture;

[[vk::binding(1)]]
Texture2D<float4> ssrTexture;

[[vk::binding(2)]]
Texture2D<float> aoTexture;

[[vk::binding(3)]]
Texture2D<float4> bloomTexture;

[[vk::binding(4)]]
SamplerState sampleSampler;

float4 main(VSOutput input) : SV_Target {
    float3 scene = sceneTexture.Sample(sampleSampler, input.fragTexCoord).rgb;
    float4 ssr = ssrTexture.Sample(sampleSampler, input.fragTexCoord);
    float ao = aoTexture.Sample(sampleSampler, input.fragTexCoord);
    float3 bloom = bloomTexture.Sample(sampleSampler, input.fragTexCoord).rgb;
    
    float3 combined = scene * ao + ssr.rgb * ssr.a + bloom;
    return float4(combined, 1.0);
}
