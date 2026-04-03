#pragma pack_matrix(row_major)

struct PointLight {
    float4 positionRadius;
    float4 colorIntensity;
    float4 shadowParams;
    uint4 shadowData;
};

struct LightsUBO {
    PointLight pointLights[64];
    uint4 numPointLights;
};

[[vk::binding(0)]]
ConstantBuffer<LightsUBO> lightsUBO;

[[vk::binding(1)]]
Texture2D<float> gBufferDepth;

[[vk::binding(2)]]
Texture2D<float4> gBufferNormal;

[[vk::binding(3)]]
TextureCube<float> shadowMaps[64];

[[vk::binding(4)]]
RWTexture2DArray<float> shadowTexture;

[[vk::binding(5)]]
SamplerState sampleSampler;

struct PushConstants {
    float4x4 invView;
    float4x4 invProj;
    float3 camPos;
    uint shadowSamples;
};
[[vk::push_constant]] PushConstants pc;

static const uint INVALID_SHADOW_INDEX = 0xFFFFFFFF;

static const float2 diskOffsets[16] = {
    float2(-0.9420162, -0.3990622),
    float2( 0.9455861, -0.7689073),
    float2(-0.0941841, -0.9293887),
    float2( 0.3449594,  0.2938776),
    float2(-0.9158858,  0.4577143),
    float2(-0.8154423, -0.8791246),
    float2(-0.3827754,  0.2767685),
    float2( 0.9748440,  0.7564838),
    float2( 0.4432332, -0.9751155),
    float2( 0.5374298, -0.4737342),
    float2(-0.2649691, -0.4189302),
    float2( 0.7919751,  0.1909019),
    float2(-0.2418884,  0.9970651),
    float2(-0.8140996,  0.9143759),
    float2( 0.1998413,  0.7864137),
    float2( 0.1438316, -0.1410079)
};

static const uint sampleOrder[16] = {
    15, 3, 10, 6,
    9, 11, 14, 2,
    0, 4, 8, 12,
    1, 5, 7, 13
};

float worldAngle(float3 worldPos) {
    return frac(sin(dot(worldPos, float3(127.1, 311.7, 74.7))) * 43758.5453) * 6.28318;
}

float3 reconstructPosition(float2 uv, float depth) {
    float4 ndc = float4(uv * 2.0 - 1.0, depth, 1.0);
    float4 viewPos = mul(ndc, pc.invProj);
    viewPos /= viewPos.w;
    float4 worldPos = mul(viewPos, pc.invView);
    return worldPos.xyz;
}

float computePointShadow(PointLight light, float3 fragPos, float3 geomNormal, float3 lightDir) {
    uint shadowIndex = light.shadowData.x;
    uint hasShadow = light.shadowData.y;
    if (shadowIndex == INVALID_SHADOW_INDEX || hasShadow == 0) {
        return 1.0;
    }
    float3 lightPos = light.positionRadius.xyz;
    float3 toFrag = fragPos - lightPos;
    float currentDistance = length(toFrag);
    float farPlane = light.shadowParams.y;
    float nearPlane = light.shadowParams.z;
    float baseBias = light.shadowParams.x;
    float shadowFadeStart = nearPlane * 10.0;
    if (currentDistance <= nearPlane || currentDistance > farPlane) {
        return 1.0;
    }
    float shadowFade = saturate((currentDistance - nearPlane) / (shadowFadeStart - nearPlane));
    float currentDepth = currentDistance / farPlane;
    float3 sampleDir = normalize(toFrag);
    float NdotL = max(dot(geomNormal, lightDir), 0.0);
    float slopeBias = baseBias * (1.0 - NdotL) * 2.0;
    float distanceBias = baseBias * (nearPlane / max(currentDistance, nearPlane));
    float bias = baseBias + slopeBias + distanceBias;
    uint requestedSamples = min(max(pc.shadowSamples, 1u), 16u);
    uint actualSamples = min(requestedSamples, 8u);
    float shadow = 0.0;
    float totalWeight = 0.0;
    float diskRadius = 0.02 + 0.04 * (currentDistance / farPlane) * sqrt(float(requestedSamples) / 16.0);
    float penumbraSize = 0.015 * (currentDistance / farPlane);
    float angle = worldAngle(fragPos);
    float cosA = 0.0f, sinA = 0.0f;
    sincos(angle, sinA, cosA);
    float3 up = abs(sampleDir.z) < 0.999 ? float3(0,0,1) : float3(1,0,0);
    float3 right = normalize(cross(up, sampleDir));
    float3 forward = cross(sampleDir, right);
    for (uint i = 0; i < actualSamples; ++i) {
        uint sampleIndex = sampleOrder[min(i, 15u)];
        float2 o = diskOffsets[sampleIndex];
        float2 rotated = float2(o.x * cosA - o.y * sinA, o.x * sinA + o.y * cosA);
        float3 offsetDir = right * rotated.x + forward * rotated.y;
        float3 sampleOffset = sampleDir + offsetDir * diskRadius;
        float sampleDepth = shadowMaps[shadowIndex].SampleLevel(sampleSampler, sampleOffset, 0.0);
        float weight = 1.0;
        float diff = (currentDepth - bias) - sampleDepth;
        shadow += smoothstep(0.0, penumbraSize, diff) * weight;
        totalWeight += weight;
    }
    shadow /= max(totalWeight, 1e-5);
    shadow *= shadowFade;
    return 1.0 - shadow;
}

[numthreads(8, 8, 1)]
void main(uint3 globalID : SV_DispatchThreadID) {
    uint width, height, layers;
    shadowTexture.GetDimensions(width, height, layers);
    if (globalID.x >= width || globalID.y >= height || globalID.z >= layers) {
        return;
    }
    uint2 pixel = globalID.xy;
    uint shadowLayer = globalID.z;
    float2 uv = (float2(pixel) + 0.5f) / float2(width, height);

    float depth = gBufferDepth.SampleLevel(sampleSampler, uv, 0.0);
    if (depth >= 0.9999) {
        shadowTexture[int3(pixel, shadowLayer)] = 1.0;
        return;
    }

    uint numLights = lightsUBO.numPointLights.x;
    if (shadowLayer >= numLights) {
        shadowTexture[int3(pixel, shadowLayer)] = 1.0;
        return;
    }

    PointLight light = lightsUBO.pointLights[shadowLayer];
    if (light.shadowData.y == 0 || light.shadowData.x != shadowLayer) {
        shadowTexture[int3(pixel, shadowLayer)] = 1.0;
        return;
    }

    float3 fragPos = reconstructPosition(uv, depth);
    float3 lightPos = light.positionRadius.xyz;
    float3 toLight = lightPos - fragPos;
    float distance = length(toLight);
    float farPlane = light.shadowParams.y;
    float nearPlane = light.shadowParams.z;
    if (distance < 0.001 || distance <= nearPlane || distance > farPlane) {
        shadowTexture[int3(pixel, shadowLayer)] = 1.0;
        return;
    }

    float3 rawNormal = gBufferNormal.SampleLevel(sampleSampler, uv, 0.0).xyz * 2.0 - 1.0;
    float normalLen = length(rawNormal);
    float3 geomNormal = (normalLen > 0.001) ? (rawNormal / normalLen) : float3(0.0, 1.0, 0.0);
    float3 L = toLight / distance;
    float shadow = computePointShadow(light, fragPos, geomNormal, L);
    shadowTexture[int3(pixel, shadowLayer)] = shadow;
}
