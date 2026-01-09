#pragma pack_matrix(row_major)

struct VSOutput {
    float4 gl_Position : SV_Position;
    [[vk::location(0)]] float2 fragTexCoord : TEXCOORD2;
};

[[vk::binding(0)]]
Texture2D<float> depthTexture;

[[vk::binding(1)]]
Texture2D<float4> normalTexture;

[[vk::binding(2)]] 
SamplerState sampleSampler;

struct PushConstants {
    float4x4 invProj;
    float4x4 proj;
    float radius;
    float bias;
    float intensity;
    uint kernelSize;
};

[[vk::push_constant]] PushConstants pc;

static const float3 kernel[16] = {
    float3(0.5381, 0.1856, -0.4319), float3(0.1379, 0.2486, 0.4430),
    float3(0.3371, 0.5679, -0.0057), float3(-0.6999, -0.0451, -0.0019),
    float3(0.0689, -0.1598, -0.8547), float3(0.0560, 0.0069, -0.1843),
    float3(-0.0146, 0.1402, 0.0762), float3(0.0100, -0.1924, -0.0344),
    float3(-0.3577, -0.5301, -0.4358), float3(-0.3169, 0.1063, 0.0158),
    float3(0.0103, -0.5869, 0.0046), float3(-0.0897, -0.4940, 0.3287),
    float3(0.7119, -0.0154, -0.0918), float3(-0.0533, 0.0596, -0.5411),
    float3(0.0352, -0.0631, 0.5460), float3(-0.4776, 0.2847, -0.0271)
};

float3 reconstructPosition(float2 uv, float depth) {
    float4 ndc = float4(uv * 2.0 - 1.0, depth, 1.0);
    float4 viewPos = mul(ndc, pc.invProj);
    viewPos /= viewPos.w;
    return viewPos.xyz;
}

float3x3 createTBN(float3 normal, float2 uv) {
    float3 randomVec = normalize(float3(
        frac(sin(dot(uv ,float2(12.9898,78.233))) * 43758.5453),
        frac(sin(dot(uv ,float2(93.9898,67.345))) * 24634.6345),
        frac(sin(dot(uv ,float2(45.332,12.345))) * 56445.2345)
    ) * 2.0 - 1.0);
    float3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
    float3 bitangent = cross(normal, tangent);
    return float3x3(tangent, bitangent, normal);
}

float4 main(VSOutput input) : SV_Target {
    float2 uv = input.fragTexCoord;
    float centerDepth = depthTexture.Sample(sampleSampler, uv);
    if (centerDepth >= 1.0) {
        return float4(1.0, 0.0, 0.0, 1.0);
    }
    float3 centerPos = reconstructPosition(uv, centerDepth);
    float3 centerNormal = normalize(normalTexture.Sample(sampleSampler, uv).xyz * 2.0 - 1.0);
    float3x3 TBN = createTBN(centerNormal, uv);

    float occlusion = 0.0;
    float validSamples = 0.0;
    for (uint i = 0; i < min(pc.kernelSize, 16); ++i) {
        float3 sampleVec = mul(kernel[i], TBN) * pc.radius;
        float3 samplePos = centerPos + sampleVec;

        float4 offsetPos = mul(float4(samplePos, 1.0), pc.proj);
        offsetPos /= offsetPos.w;
        float2 sampleUV = offsetPos.xy * 0.5 + 0.5;

        if (any(sampleUV < 0.0) || any(sampleUV > 1.0)) {
            continue;
        }
        
        float sampleDepth = depthTexture.Sample(sampleSampler, sampleUV);
        float3 sampleViewPos = reconstructPosition(sampleUV, sampleDepth);
        float rangeCheck = smoothstep(0.0, 1.0, pc.radius / abs(length(sampleViewPos.z - centerPos.z)));
        float occluded = (sampleViewPos.z >= samplePos.z + pc.bias) ? 1.0 : 0.0;
        occlusion += occluded * rangeCheck;
        validSamples += 1.0;
    }
    occlusion = 1.0 - (occlusion / (validSamples + 1e-5)) * pc.intensity;
    return float4(occlusion, 0.0, 0.0, 1.0);
}
