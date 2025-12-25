#pragma pack_matrix(row_major)

struct VSOutput {
    [[vk::location(0)]] float2 fragTexCoord : TEXCOORD2;
};

[[vk::binding(0)]]
Texture2D<float4> lightingTexture;

[[vk::binding(1)]]
Texture2D<float> gBufferDepth;

[[vk::binding(2)]]
Texture2D<float4> gBufferNormal;

[[vk::binding(3)]]
SamplerState sampleSampler;

struct PushConstants {
    float4x4 view;
    float4x4 proj;
    float4x4 invView;
    float4x4 invProj;
};

[[vk::push_constant]] PushConstants pc;

static const int MAX_STEPS = 150;
static const float MAX_DISTANCE = 150.0;
static const float START_STEP = 0.05;
static const float THICKNESS = 0.5;
static const int NUM_BINARY_SEARCH_STEPS = 5;

float hash(float2 p) {
    return frac(sin(dot(p, float2(12.9898, 78.233))) * 43758.5453);
}

float3 reconstructPosition(float2 uv, float depth) {
    float4 ndc = float4(uv * 2.0 - 1.0, depth, 1.0);
    float4 viewPos = mul(ndc, pc.invProj);
    viewPos /= viewPos.w;
    return viewPos.xyz;
}

float3 viewToScreenSpace(float3 viewPos) {
    float4 clipSpace = mul(float4(viewPos, 1.0), pc.proj);
    clipSpace /= clipSpace.w;
    float3 screenSpace;
    screenSpace.xy = clipSpace.xy * 0.5 + 0.5;
    screenSpace.z = clipSpace.z;
    return screenSpace;
}

bool rayMarch(float3 rayStart, float3 rayDir, float jitter, out float2 hitUV, out float hitDepth, out float rayLength) {
    hitUV = float2(0.0, 0.0);
    hitDepth = 0.0;
    rayLength = 0.0;
    float3 currPos = rayStart;
    float stepSize = START_STEP;
    float3 rayDirNorm = normalize(rayDir);
    currPos += rayDirNorm * stepSize * jitter;
    float3 prevPos = currPos;
    for (int i = 0; i < MAX_STEPS; i++) {
        float dist = length(currPos - rayStart);
        if (dist > MAX_DISTANCE) {
            return false;
        }
        stepSize = lerp(stepSize, THICKNESS, dist / MAX_DISTANCE);
        float3 stepDir = rayDirNorm * stepSize;
        currPos += stepDir;
        float3 screenPos = viewToScreenSpace(currPos);
        if (screenPos.x < 0.0 || screenPos.x > 1.0
         || screenPos.y < 0.0 || screenPos.y > 1.0
         || screenPos.z < 0.0 || screenPos.z > 1.0) {
            return false;
        }
        float sampledDepth = gBufferDepth.SampleLevel(sampleSampler, screenPos.xy, 0);
        float3 sampledPos = reconstructPosition(screenPos.xy, sampledDepth);
        float depthDiff = sampledPos.z - currPos.z;
        float adaptiveThickness = max(THICKNESS, stepSize * 1.5);
        if (depthDiff > 0.0 && depthDiff < adaptiveThickness) {
            float3 searchStart = prevPos;
            float3 searchEnd = currPos;
            for (int j = 0; j < NUM_BINARY_SEARCH_STEPS; j++) {
                float3 midPos = (searchStart + searchEnd) * 0.5;
                float3 midScreenPos = viewToScreenSpace(midPos);
                float midSampledDepth = gBufferDepth.SampleLevel(sampleSampler, midScreenPos.xy, 0);
                float3 midSampledPos = reconstructPosition(midScreenPos.xy, midSampledDepth);
                float midDepthDiff = midSampledPos.z - midPos.z;
                if (midDepthDiff > 0.0 && midDepthDiff < adaptiveThickness) {
                    searchEnd = midPos;
                } else {
                    searchStart = midPos;
                }
            }
            float3 finalPos = (searchStart + searchEnd) * 0.5;
            float3 finalScreenPos = viewToScreenSpace(finalPos);
            hitUV = finalScreenPos.xy;
            hitDepth = finalScreenPos.z;
            rayLength = length(finalPos - rayStart);
            return true;
        }
        prevPos = currPos;
    }
    return false;
}

float4 main(VSOutput input) : SV_Target {
    uint width, height;
    lightingTexture.GetDimensions(width, height);
    uint2 gid = uint2(input.fragTexCoord * float2(width, height));
    float2 uv = (float2(gid.x, gid.y) + 0.5) / float2(width, height);
    float depth = gBufferDepth.Load(int3(gid.x, gid.y, 0));
    if (depth >= 0.9999) {
        return float4(0.0, 0.0, 0.0, 1.0);
    }
    float3 normal = normalize(gBufferNormal.Load(int3(gid.x, gid.y, 0)).xyz * 2.0 - 1.0);
    float3 viewPos = reconstructPosition(uv, depth);
    float3 viewDir = normalize(-viewPos);
    float3 normalView = mul(float4(normal, 0.0), pc.view).xyz;
    normalView = normalize(normalView);
    float3 reflectDir = reflect(-viewDir, normalView);
    if (reflectDir.z > 0.0) {
        return float4(0.0, 0.0, 0.0, 1.0);
    }
    float facingRatio = max(dot(normalView, viewDir), 0.0);
    if (facingRatio < 0.1) {
        return float4(0.0, 0.0, 0.0, 1.0);
    }
    float jitter = hash(uv + frac(width * 0.001));
    float2 hitUV;
    float hitDepth;
    float rayLength;
    bool hit = rayMarch(viewPos, reflectDir, jitter, hitUV, hitDepth, rayLength);
    if (hit) {
        float mipLevel = clamp(rayLength * 0.1, 0.0, 4.0);
        float4 reflectionColor = lightingTexture.SampleLevel(sampleSampler, hitUV, mipLevel);
        float2 edgeFade = smoothstep(0.0, 0.1, hitUV) * smoothstep(1.0, 0.9, hitUV);
        float edgeFactor = min(edgeFade.x, edgeFade.y);
        float fresnelFactor = pow(1.0 - facingRatio, 3.0);
        float distanceFade = 1.0 - smoothstep(MAX_DISTANCE * 0.7, MAX_DISTANCE, rayLength);
        float finalAlpha = reflectionColor.a * edgeFactor * fresnelFactor * distanceFade;
        return float4(reflectionColor.rgb, finalAlpha);
    } else {
        return float4(0.0, 0.0, 0.0, 1.0);
    }
}