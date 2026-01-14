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
    float4x4 view;
    uint flag;
    uint pad[3];
};

static const float RADIUS = 1.0;
static const float BIAS = 0.025;
static const float INTENSITY = 2.0;

[[vk::push_constant]] PushConstants pc;

static const float3 kernel[16] = {
    float3(0.5381, 0.1856, 0.4319), float3(0.1379, 0.2486, 0.4430),
    float3(0.3371, 0.5679, 0.1057), float3(-0.6999, -0.0451, 0.1019),
    float3(0.0689, -0.1598, 0.8547), float3(0.0560, 0.0069, 0.1843),
    float3(-0.0146, 0.1402, 0.0762), float3(0.0100, -0.1924, 0.2344),
    float3(-0.3577, -0.5301, 0.4358), float3(-0.3169, 0.1063, 0.1158),
    float3(0.0103, -0.5869, 0.2046), float3(-0.0897, -0.4940, 0.3287),
    float3(0.7119, -0.0154, 0.1918), float3(-0.0533, 0.0596, 0.5411),
    float3(0.0352, -0.0631, 0.5460), float3(-0.4776, 0.2847, 0.2271)
};

float3 reconstructPosition(float2 uv, float depth) {
    float4 ndc = float4(uv * 2.0 - 1.0, depth, 1.0);
    float4 viewPos = mul(ndc, pc.invProj);
    viewPos /= viewPos.w;
    return viewPos.xyz;
}

float3x3 createTBN(float3 normal, float2 uv) {
    float3 randomVec = normalize(float3(
        frac(sin(dot(uv, float2(12.9898, 78.233))) * 43758.5453),
        frac(sin(dot(uv, float2(93.9898, 67.345))) * 24634.6345),
        frac(sin(dot(uv, float2(45.332, 12.345))) * 56445.2345)
    ) * 2.0 - 1.0);
    float3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
    float3 bitangent = cross(normal, tangent);
    return float3x3(tangent, bitangent, normal);
}

float computeSSAO(float2 uv, float3 centerPos, float3 centerNormal) {
    float3x3 TBN = createTBN(centerNormal, uv);
    float occlusion = 0.0;

    const int numSamples = 16;

    for (uint i = 0; i < numSamples; ++i) {
        float3 sampleVec = mul(kernel[i], TBN) * RADIUS;
        float3 samplePos = centerPos + sampleVec;

        float4 offsetPos = mul(float4(samplePos, 1.0), pc.proj);
        offsetPos /= offsetPos.w;
        float2 sampleUV = offsetPos.xy * 0.5 + 0.5;
        float expectedDepth = offsetPos.z;

        if (any(sampleUV < 0.0) || any(sampleUV > 1.0)) {
            continue;
        }
        
        float actualDepth = depthTexture.Sample(sampleSampler, sampleUV);
        if (actualDepth >= 1.0 || actualDepth <= 0.0) {
            continue;
        }
        
        float depthDiscontinuity = expectedDepth - actualDepth;
        float edgeFade = 1.0;
        if (depthDiscontinuity > 0.002) {
            float3 actualViewPos = reconstructPosition(sampleUV, actualDepth);
            float distToActual = length(actualViewPos - centerPos);
            edgeFade = saturate(1.0 - (distToActual - RADIUS) / RADIUS);
            if (distToActual > RADIUS * 1.5) {
                continue;
            }
        }
        
        float3 sampleViewPos = reconstructPosition(sampleUV, actualDepth);
        float3 v = sampleViewPos - centerPos;
        float dist = length(v);
        float rangeCheck = smoothstep(0.0, 1.0, RADIUS / (dist + 1e-5));
        
        float horizon = dot(normalize(v), centerNormal);
        
        if (horizon > BIAS) {
            occlusion += horizon * rangeCheck * edgeFade;
        }
    }
    
    occlusion = occlusion / float(numSamples);
    float ao = 1.0 - saturate(occlusion * INTENSITY);
    return ao;
}

float rand(float2 uv) {
    return frac(sin(dot(uv, float2(12.9898,78.233))) * 43758.5453);
}

float computeGTAO(float2 uv, float3 centerPos, float3 centerNormal) {
    float3x3 TBN = createTBN(centerNormal, uv);
    float occlusion = 0.0;
    
    const int numDirections = 8;
    const int numSteps = 6;
    const float stepSize = RADIUS / float(numSteps);

    const float TWOPI = 6.28318530718;
    float randAngle = rand(uv) * TWOPI;
    
    for (int d = 0; d < numDirections; ++d) {
        float angle = randAngle + (float(d) / float(numDirections)) * TWOPI;
        float3 dir = normalize(cos(angle) * TBN[0] + sin(angle) * TBN[1]);
        
        float maxHorizon = 0.0;
        
        for (int s = 1; s <= numSteps; ++s) {
            float3 samplePos = centerPos + dir * (float(s) * stepSize);
            
            float4 offsetPos = mul(float4(samplePos, 1.0), pc.proj);
            offsetPos /= offsetPos.w;
            float2 sampleUV = offsetPos.xy * 0.5 + 0.5;
            float expectedDepth = offsetPos.z;

            if (any(sampleUV < 0.0) || any(sampleUV > 1.0)) {
                continue;
            }

            float actualDepth = depthTexture.Sample(sampleSampler, sampleUV);
            if (actualDepth >= 1.0 || actualDepth <= 0.0) {
                continue;
            }

            float depthDiscontinuity = expectedDepth - actualDepth;
            float edgeFade = 1.0;
            if (depthDiscontinuity > 0.002) {
                float3 actualViewPos = reconstructPosition(sampleUV, actualDepth);
                float distToActual = length(actualViewPos - centerPos);
                edgeFade = saturate(1.0 - (distToActual - RADIUS) / RADIUS);
                if (distToActual > RADIUS * 1.5) {
                    continue;
                }
            }

            float3 sampleViewPos = reconstructPosition(sampleUV, actualDepth);
            float3 v = sampleViewPos - centerPos;
            float dist = length(v);
            
            float rangeCheck = smoothstep(0.0, 1.0, RADIUS / (dist + 1e-5));
            float horizon = dot(normalize(v), centerNormal);
            
            if (horizon > BIAS) {
                maxHorizon = max(maxHorizon, (horizon - BIAS) * rangeCheck * edgeFade);
            }
        }
        
        occlusion += maxHorizon;
    }
    
    occlusion = occlusion / float(numDirections);
    float ao = 1.0 - saturate(occlusion * INTENSITY);
    return ao;
}

float4 main(VSOutput input) : SV_Target {
    float2 uv = input.fragTexCoord;
    float centerDepth = depthTexture.Sample(sampleSampler, uv);
    if (centerDepth >= 1.0 || pc.flag == 0) {
        return float4(1.0, 0.0, 0.0, 1.0);
    }
    float3 centerPos = reconstructPosition(uv, centerDepth);
    
    float3 worldNormal = normalize(normalTexture.Sample(sampleSampler, uv).xyz * 2.0 - 1.0);
    float3 centerNormal = normalize(mul(float4(worldNormal, 0.0), pc.view).xyz);
    
    float occlusion = 0.0;
    if (pc.flag == 1) {
        occlusion = computeSSAO(uv, centerPos, centerNormal);
    } else if (pc.flag == 2) {
        occlusion = computeGTAO(uv, centerPos, centerNormal);
    }
    return float4(occlusion, 0.0, 0.0, 1.0);
}
