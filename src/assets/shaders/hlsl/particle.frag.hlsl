struct VSOutput {
    [[vk::location(0)]] float2 uv : TEXCOORD0;
    [[vk::location(1)]] float age : TEXCOORD1;
    [[vk::location(2)]] float4 color : COLOR;
};

[[vk::binding(1)]] Texture2D<float> gbufferDepth;
[[vk::binding(2)]] SamplerState gbufferSampler;

struct PushConstants {
    float4x4 viewProj;
    float2 screenSize;
    float particleSize;
    float streakScale;
};

[[vk::push_constant]] PushConstants pc;

float4 main(VSOutput input, float4 fragCoord : SV_Position) : SV_Target {
    float2 screenUV = fragCoord.xy / pc.screenSize;
    float sceneDepth = gbufferDepth.Sample(gbufferSampler, screenUV);
    if (sceneDepth < 1.0 && fragCoord.z > sceneDepth) {
        discard;
    }
    
    float4 fragColor = input.color;
    
    if (fragColor.a < 0.0) { // trail particle
        float edgeFade = 1.0 - abs(input.uv.x * 2.0 - 1.0);
        edgeFade = pow(edgeFade, 0.5);
        float ageFade = 1.0 - input.age;
        float coreness = pow(edgeFade, lerp(4.0, 8.0, input.age));
        fragColor.rgb = lerp(fragColor.rgb, float3(1.0, 1.0, 1.0), coreness);
        fragColor.rgb *= 2.0;
        fragColor.a = edgeFade * edgeFade * 0.5 + ageFade * 0.5;
        return fragColor;
    }
    
    float2 centered = input.uv * 2.0 - 1.0;
    float distX = abs(centered.x);
    float distY = abs(centered.y);
    float dist = length(float2(distX * 2.0, distY));
    float coreFalloff = saturate(1.0 - dist * 1.5);
    float glowFalloff = saturate(1.0 - dist * 0.8);
    float falloff = coreFalloff * 0.7 + glowFalloff * 0.3;
    falloff = pow(falloff, 1.5);
    float ageFade = 1.0 - input.age;
    ageFade = pow(ageFade, 0.5);
    float coreness = pow(coreFalloff, 4.0);
    fragColor.rgb = lerp(fragColor.rgb, float3(1.0, 1.0, 1.0), coreness);
    fragColor.rgb *= 1.0 + coreFalloff * 2.0;
    fragColor.a *= falloff * ageFade;
    return fragColor;
}