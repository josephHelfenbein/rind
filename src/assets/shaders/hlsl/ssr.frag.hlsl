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
static const int NUM_BINARY_SEARCH_STEPS = 5;

float hash(float2 p) {
    return frac(sin(dot(p, float2(12.9898, 78.233))) * 43758.5453);
}

float3 reconstructViewPos(float2 uv, float depth) {
    float4 ndc = float4(uv * 2.0 - 1.0, depth, 1.0);
    float4 viewPos = mul(ndc, pc.invProj);
    viewPos /= viewPos.w;
    return viewPos.xyz;
}

bool rayMarch(float3 startScreen, float3 screenStep, int stepCount, float jitter, out float2 hitUV, out float hitDepth) {
    hitUV = float2(0, 0);
    hitDepth = 0;
    float thickness = max(0.002, abs(screenStep.z) * 2.0);
    float3 curr = startScreen + screenStep * jitter;
    float3 prev = curr;

    [loop]
    for (int i = 0; i < stepCount; i++) {
        prev = curr;
        curr += screenStep;

        if (curr.x < 0.0 || curr.x > 1.0
         || curr.y < 0.0 || curr.y > 1.0
         || curr.z < 0.0 || curr.z > 1.0) {
            return false;
        }

        float sampledDepth = gBufferDepth.SampleLevel(sampleSampler, curr.xy, 0);
        float depthDiff = curr.z - sampledDepth;

        if (depthDiff > 0.0 && depthDiff < thickness) {
            float3 lo = prev, hi = curr;
            for (int j = 0; j < NUM_BINARY_SEARCH_STEPS; j++) {
                float3 mid = (lo + hi) * 0.5;
                float midSampled = gBufferDepth.SampleLevel(sampleSampler, mid.xy, 0);
                float midDiff = mid.z - midSampled;
                if (midDiff > 0.0 && midDiff < thickness) {
                    hi = mid;
                } else {
                    lo = mid;
                }
            }
            hitUV = (lo.xy + hi.xy) * 0.5;
            hitDepth = (lo.z + hi.z) * 0.5;
            return true;
        }
    }
    return false;
}

float4 main(VSOutput input) : SV_Target {
    uint width, height;
    lightingTexture.GetDimensions(width, height);
    float2 screenSize = float2(width, height);
    uint2 gid = uint2(input.fragTexCoord * screenSize);
    float2 uv = (float2(gid) + 0.5) / screenSize;

    float depth = gBufferDepth.Load(int3(gid.x, gid.y, 0));
    if (depth >= 0.9999) {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    float3 normal = normalize(gBufferNormal.Load(int3(gid.x, gid.y, 0)).xyz * 2.0 - 1.0);
    float3 viewPos = reconstructViewPos(uv, depth);
    float3 viewDir = normalize(-viewPos);
    float3 normalView = normalize(mul(float4(normal, 0.0), pc.view).xyz);
    float3 reflectDir = reflect(-viewDir, normalView);

    if (reflectDir.z > 0.0) {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    float facingRatio = max(dot(normalView, viewDir), 0.0);
    if (facingRatio < 0.1) {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    float4 startClip = mul(float4(viewPos, 1.0), pc.proj);
    float3 startScreen;
    startScreen.xy = startClip.xy / startClip.w * 0.5 + 0.5;
    startScreen.z = startClip.z / startClip.w;

    float3 endView = viewPos + reflectDir * MAX_DISTANCE;
    float4 endClip = mul(float4(endView, 1.0), pc.proj);
    if (endClip.w < 0.01) {
        float t = (0.01 - startClip.w) / (endClip.w - startClip.w);
        endView = viewPos + reflectDir * MAX_DISTANCE * t * 0.99;
        endClip = mul(float4(endView, 1.0), pc.proj);
    }

    float3 endScreen;
    endScreen.xy = endClip.xy / endClip.w * 0.5 + 0.5;
    endScreen.z = endClip.z / endClip.w;

    float3 screenDelta = endScreen - startScreen;
    float2 pixelDelta = screenDelta.xy * screenSize;
    int stepCount = clamp(int(max(abs(pixelDelta.x), abs(pixelDelta.y))), 16, MAX_STEPS);
    float3 screenStep = screenDelta / float(stepCount);

    float jitter = hash(uv + frac(width * 0.001));
    float2 hitUV;
    float hitNdcDepth;
    bool hit = rayMarch(startScreen, screenStep, stepCount, jitter, hitUV, hitNdcDepth);
    if (hit) {
        float3 hitViewPos = reconstructViewPos(hitUV, hitNdcDepth);
        float rayLength = length(hitViewPos - viewPos);

        float mipLevel = clamp(rayLength * 0.1, 0.0, 4.0);
        float4 reflectionColor = lightingTexture.SampleLevel(sampleSampler, hitUV, mipLevel);
        float2 edgeFade = smoothstep(0.0, 0.1, hitUV) * smoothstep(1.0, 0.9, hitUV);
        float edgeFactor = min(edgeFade.x, edgeFade.y);
        float fresnelFactor = pow(1.0 - facingRatio, 3.0);
        float distanceFade = 1.0 - smoothstep(MAX_DISTANCE * 0.7, MAX_DISTANCE, rayLength);
        float finalAlpha = reflectionColor.a * edgeFactor * fresnelFactor * distanceFade;
        return float4(reflectionColor.rgb, finalAlpha);
    }

    return float4(0.0, 0.0, 0.0, 0.0);
}